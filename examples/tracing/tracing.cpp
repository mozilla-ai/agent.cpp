#include "agent.h"
#include "calculator_tool.h"
#include "callbacks.h"
#include "chat.h"
#include "chat_loop.h"
#include "error.h"
#include "error_recovery_callback.h"
#include "llama.h"
#include "logging_callback.h"
#include "model.h"
#include "tool.h"

#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/resource/semantic_conventions.h>
#include <opentelemetry/sdk/trace/processor.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/scope.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/tracer.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace resource = opentelemetry::sdk::resource;
namespace otlp = opentelemetry::exporter::otlp;

static std::shared_ptr<trace_sdk::TracerProvider> g_tracer_provider;

void
InitTracer(const std::string& endpoint)
{
    otlp::OtlpHttpExporterOptions opts;
    opts.url = endpoint;

    auto exporter = otlp::OtlpHttpExporterFactory::Create(opts);
    auto processor =
      trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));

    // Create resource with service name
    auto resource_attributes = resource::ResourceAttributes{
        { resource::SemanticConventions::kServiceName, "agent.cpp" }
    };
    auto service_resource = resource::Resource::Create(resource_attributes);

    g_tracer_provider = trace_sdk::TracerProviderFactory::Create(
      std::move(processor), service_resource);

    std::shared_ptr<trace_api::TracerProvider> api_provider = g_tracer_provider;
    trace_api::Provider::SetTracerProvider(api_provider);
}

void
CleanupTracer()
{
    if (g_tracer_provider) {
        g_tracer_provider->ForceFlush();
    }
    g_tracer_provider.reset();
    std::shared_ptr<trace_api::TracerProvider> none;
    trace_api::Provider::SetTracerProvider(none);
}

static opentelemetry::nostd::shared_ptr<trace_api::Tracer>
GetTracer()
{
    return trace_api::Provider::GetTracerProvider()->GetTracer("agent.cpp",
                                                               "0.1.0");
}

class OpenTelemetryCallback : public agent_cpp::Callback
{
  private:
    std::string model_name;
    std::string provider_name;
    std::string agent_name;

    opentelemetry::nostd::shared_ptr<trace_api::Tracer> tracer;
    opentelemetry::nostd::shared_ptr<trace_api::Span> agent_span;
    opentelemetry::nostd::shared_ptr<trace_api::Span> llm_span;
    opentelemetry::nostd::shared_ptr<trace_api::Span> tool_span;

    std::unique_ptr<trace_api::Scope> agent_scope;
    std::unique_ptr<trace_api::Scope> llm_scope;
    std::unique_ptr<trace_api::Scope> tool_scope;

  public:
    explicit OpenTelemetryCallback(const std::string& model,
                                   const std::string& provider = "llama.cpp",
                                   const std::string& agent = "agent.cpp")
      : model_name(model)
      , provider_name(provider)
      , agent_name(agent)
      , tracer(GetTracer())
    {
    }

    // https://opentelemetry.io/docs/specs/semconv/gen-ai/gen-ai-agent-spans/
    void before_agent_loop(std::vector<common_chat_msg>& /*messages*/) override
    {
        std::string span_name = "invoke_agent " + agent_name;

        trace_api::StartSpanOptions options;
        options.kind = trace_api::SpanKind::kInternal;

        agent_span = tracer->StartSpan(span_name, options);
        agent_scope = std::make_unique<trace_api::Scope>(agent_span);

        agent_span->SetAttribute("gen_ai.operation.name", "invoke_agent");
        agent_span->SetAttribute("gen_ai.provider.name", provider_name);
        agent_span->SetAttribute("gen_ai.agent.name", agent_name);
        agent_span->SetAttribute("gen_ai.request.model", model_name);
    }

    void after_agent_loop(std::vector<common_chat_msg>& /*messages*/,
                          std::string& /*response*/) override
    {
        if (agent_span) {
            agent_scope.reset();
            agent_span->End();
            agent_span = nullptr;
        }
    }

    // https://opentelemetry.io/docs/specs/semconv/gen-ai/gen-ai-spans/
    void before_llm_call(std::vector<common_chat_msg>& /*messages*/) override
    {
        std::string span_name = "chat " + model_name;

        trace_api::StartSpanOptions options;
        options.kind = trace_api::SpanKind::kClient;

        llm_span = tracer->StartSpan(span_name, options);
        llm_scope = std::make_unique<trace_api::Scope>(llm_span);

        llm_span->SetAttribute("gen_ai.operation.name", "chat");
        llm_span->SetAttribute("gen_ai.provider.name", provider_name);
        llm_span->SetAttribute("gen_ai.request.model", model_name);
    }

    void after_llm_call(common_chat_msg& parsed_msg) override
    {
        if (llm_span) {
            std::string finish_reason =
              parsed_msg.tool_calls.empty() ? "stop" : "tool_calls";
            llm_span->SetAttribute("gen_ai.response.finish_reasons",
                                   finish_reason);
            llm_span->SetAttribute("gen_ai.output.type", "text");

            llm_scope.reset();
            llm_span->End();
            llm_span = nullptr;
        }
    }

    // https://opentelemetry.io/docs/specs/semconv/gen-ai/gen-ai-spans/#execute-tool-span
    void before_tool_execution(std::string& tool_name,
                               std::string& /*arguments*/) override
    {
        std::string span_name = "execute_tool " + tool_name;

        trace_api::StartSpanOptions options;
        options.kind = trace_api::SpanKind::kInternal;

        tool_span = tracer->StartSpan(span_name, options);
        tool_scope = std::make_unique<trace_api::Scope>(tool_span);

        tool_span->SetAttribute("gen_ai.operation.name", "execute_tool");
        tool_span->SetAttribute("gen_ai.tool.name", tool_name);
        tool_span->SetAttribute("gen_ai.tool.type", "function");
    }

    void after_tool_execution(std::string& /*tool_name*/,
                              std::string& result) override
    {
        if (tool_span) {
            if (result.find("\"error\"") != std::string::npos) {
                tool_span->SetAttribute("error.type", "tool_execution_error");
                tool_span->SetStatus(trace_api::StatusCode::kError,
                                     "Tool execution failed");
            }

            tool_scope.reset();
            tool_span->End();
            tool_span = nullptr;
        }
    }
};

static void
print_usage(int /*unused*/, char** argv)
{
    printf("\nexample usage:\n");
    printf("\n    %s -m model.gguf\n", argv[0]);
    printf("\n");
    printf("options:\n");
    printf("  -m <path>       Path to the GGUF model file (required)\n");
    printf("  -e <endpoint>   OTLP HTTP endpoint (default: "
           "http://localhost:4318/v1/traces)\n");
    printf("\n");
    printf("To visualize traces, run a trace collector like Jaeger:\n");
    printf("  docker run -p 16686:16686 -p 4317:4317 -p 4318:4318 "
           "jaegertracing/all-in-one\n");
    printf("\n");
}

int
main(int argc, char** argv)
{
    std::string model_path;
    std::string otlp_endpoint = "http://localhost:4318/v1/traces";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-m" || arg == "--model") && i + 1 < argc) {
            model_path = argv[++i];
        } else if ((arg == "-e" || arg == "--endpoint") && i + 1 < argc) {
            otlp_endpoint = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argc, argv);
            return 0;
        }
    }

    if (model_path.empty()) {
        print_usage(argc, argv);
        return 1;
    }

    printf("Initializing OpenTelemetry tracer...\n");
    InitTracer(otlp_endpoint);
    printf("   Using OTLP endpoint: %s\n", otlp_endpoint.c_str());

    std::string model_name = model_path;
    size_t last_slash = model_name.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        model_name = model_name.substr(last_slash + 1);
    }

    printf("Setting up tools...\n");
    std::vector<std::unique_ptr<agent_cpp::Tool>> tools;
    tools.push_back(std::make_unique<CalculatorTool>());
    printf("Configured tools: calculator\n");

    printf("Loading model...\n");
    std::shared_ptr<agent_cpp::Model> model;
    try {
        model = agent_cpp::Model::create(model_path);
    } catch (const agent_cpp::ModelError& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    printf("Model loaded and initialized successfully\n");

    std::vector<std::unique_ptr<agent_cpp::Callback>> callbacks;
    callbacks.push_back(std::make_unique<LoggingCallback>());
    callbacks.push_back(std::make_unique<ErrorRecoveryCallback>());
    callbacks.push_back(std::make_unique<OpenTelemetryCallback>(
      model_name, "llama.cpp", "agent.cpp"));

    const std::string instructions =
      "You are a helpful assistant that can solve basic calculations. "
      "When the user provides a mathematical problem, use the 'calculator' "
      "tool "
      "to compute the result. Only use the tool when necessary."
      "If the user asks a composed calculation, break it down into steps and "
      "use the tool for each step."
      "For example, if the user asks 'What is (3 + 5) * 2?', first calculate "
      "'3 + 5' using the tool, then use the result to calculate the final "
      "answer.";

    agent_cpp::Agent agent(
      std::move(model), std::move(tools), std::move(callbacks), instructions);

    agent.load_or_create_cache("tracing.cache");

    printf("\nTracing Agent ready! Try asking me to do some calculations.\n");
    printf("   Type an empty line to quit.\n\n");

    run_chat_loop(agent);

    CleanupTracer();
    return 0;
}
