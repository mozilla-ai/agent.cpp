#include "agent.h"
#include "callbacks.h"
#include "chat.h"
#include "chat_loop.h"
#include "llama.h"
#include "logging_callback.h"
#include "model.h"
#include "prompt_cache.h"
#include "tool.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unistd.h>

class MemoryStore
{
  private:
    json memories;
    std::string file_path;

  public:
    explicit MemoryStore(const std::string& path)
      : file_path(path)
    {
        load_from_file();
    }

    void write(const std::string& key, const std::string& value)
    {
        memories[key] = value;
        save_to_file();
    }

    std::string read(const std::string& key)
    {
        if (memories.contains(key)) {
            return memories[key].get<std::string>();
        }
        return "";
    }

    std::vector<std::string> list_keys()
    {
        std::vector<std::string> keys;
        for (const auto& [key, value] : memories.items()) {
            keys.push_back(key);
        }
        return keys;
    }

    bool has_key(const std::string& key) { return memories.contains(key); }

  private:
    void save_to_file()
    {
        std::ofstream file(file_path);
        if (file.is_open()) {
            file << memories.dump(4);
        }
    }

    void load_from_file()
    {
        std::ifstream file(file_path);
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            if (!content.empty()) {
                try {
                    memories = json::parse(content);
                } catch (const json::parse_error&) {
                    memories = json::object();
                }
            } else {
                memories = json::object();
            }
        } else {
            memories = json::object();
        }
    }
};

class WriteMemoryTool : public Tool
{
  private:
    std::shared_ptr<MemoryStore> store_;

  public:
    explicit WriteMemoryTool(std::shared_ptr<MemoryStore> store)
      : store_(std::move(store))
    {
    }

    common_chat_tool get_definition() const override
    {
        json schema = {
            { "type", "object" },
            { "properties",
              {
                { "key",
                  { { "type", "string" },
                    { "description",
                      "A descriptive key for the memory (e.g., 'user_name', "
                      "'favorite_color', 'birthday')" } } },
                { "value",
                  { { "type", "string" },
                    { "description", "The information to store" } } },
              } },
            { "required", { "key", "value" } }
        };

        return { "write_memory",
                 "Store information about the user for future reference. Use "
                 "this to remember important facts, preferences, or details "
                 "that the user shares. Examples: user's name, favorite color, "
                 "birthday, preferences, etc.",
                 schema.dump() };
    }

    std::string get_name() const override { return "write_memory"; }

    std::string execute(const json& arguments) override
    {
        std::string key = arguments.at("key").get<std::string>();
        std::string value = arguments.at("value").get<std::string>();

        store_->write(key, value);

        json response;
        response["success"] = true;
        response["message"] =
          "Successfully stored memory with key '" + key + "'";
        return response.dump();
    }
};

class ReadMemoryTool : public Tool
{
  private:
    std::shared_ptr<MemoryStore> store_;

  public:
    explicit ReadMemoryTool(std::shared_ptr<MemoryStore> store)
      : store_(std::move(store))
    {
    }

    common_chat_tool get_definition() const override
    {
        json schema = {
            { "type", "object" },
            { "properties",
              {
                { "key",
                  { { "type", "string" },
                    { "description", "The key of the memory to retrieve." } } },
              } },
            { "required", { "key" } }
        };

        return { "read_memory",
                 "Retrieve previously stored information about the user. Use "
                 "this to recall facts, preferences, or details that were "
                 "previously saved. You can use `list_memory` to see what keys "
                 "are available.",
                 schema.dump() };
    }

    std::string get_name() const override { return "read_memory"; }

    std::string execute(const json& arguments) override
    {
        std::string key = arguments.at("key").get<std::string>();

        json response;

        if (store_->has_key(key)) {
            std::string value = store_->read(key);
            response["success"] = true;
            response["key"] = key;
            response["value"] = value;
        } else {
            response["success"] = false;
            response["message"] = "No memory found with key '" + key + "'";

            auto keys = store_->list_keys();
            if (!keys.empty()) {
                response["available_keys"] = keys;
            }
        }

        return response.dump();
    }
};

class ListMemoryTool : public Tool
{
  private:
    std::shared_ptr<MemoryStore> store_;

  public:
    explicit ListMemoryTool(std::shared_ptr<MemoryStore> store)
      : store_(std::move(store))
    {
    }

    common_chat_tool get_definition() const override
    {
        json schema = { { "type", "object" },
                        { "properties", json::object() },
                        { "required", json::array() } };

        return {
            "list_memory",
            "List all available memory keys that have been stored. Use "
            "this to see what information has been remembered about the user.",
            schema.dump()
        };
    }

    std::string get_name() const override { return "list_memory"; }

    std::string execute(const json& /*arguments*/) override
    {
        json response;
        auto keys = store_->list_keys();

        if (keys.empty()) {
            response["success"] = true;
            response["message"] = "No memories stored yet.";
            response["keys"] = json::array();
        } else {
            response["success"] = true;
            response["message"] = "Available memory keys:";
            response["keys"] = keys;
        }

        return response.dump();
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
    printf("\n");
}

int
main(int argc, char** argv)
{
    std::string model_path;
    std::string memory_file = "memory.json";

    for (int i = 1; i < argc; i++) {
        try {
            if (strcmp(argv[i], "-m") == 0) {
                if (i + 1 < argc) {
                    model_path = argv[++i];
                } else {
                    print_usage(argc, argv);
                    return 1;
                }
            } else {
                print_usage(argc, argv);
                return 1;
            }
        } catch (std::exception& e) {
            fprintf(stderr, "error: %s\n", e.what());
            print_usage(argc, argv);
            return 1;
        }
    }

    if (model_path.empty()) {
        print_usage(argc, argv);
        return 1;
    }

    printf("Initializing memory store...\n");
    auto memory_store = std::make_shared<MemoryStore>(memory_file);
    printf("   Using storage file: %s\n", memory_file.c_str());

    printf("Setting up memory tools...\n");
    std::vector<std::unique_ptr<Tool>> tools;
    tools.push_back(std::make_unique<WriteMemoryTool>(memory_store));
    tools.push_back(std::make_unique<ReadMemoryTool>(memory_store));
    tools.push_back(std::make_unique<ListMemoryTool>(memory_store));
    printf("Configured tools: write_memory, read_memory, list_memory\n");

    printf("Loading model...\n");
    auto model = Model::create(model_path);
    if (!model) {
        fprintf(stderr, "%s: error: unable to initialize model\n", __func__);
        return 1;
    }
    printf("Model loaded and initialized successfully\n");

    const std::string instructions =
      "You are a helpful assistant with memory capabilities. "
      "You can remember information about the user using the write_memory "
      "tool "
      "and recall it later using the read_memory tool. "
      "When the user shares personal information (like their name, "
      "preferences, "
      "or important facts), you must use write_memory to store it. "
      "When needed, use list_memory to check if you have relevant stored "
      "memories.";

    std::vector<std::unique_ptr<Callback>> callbacks;
    callbacks.push_back(std::make_unique<LoggingCallback>());

    Agent agent(
      std::move(model), std::move(tools), std::move(callbacks), instructions);
    load_or_create_agent_cache(agent, "memory.cache");

    printf("\nMemory Agent ready!\n");
    printf("   Try telling me your name, preferences, or ask to remember "
           "something!\n");
    printf("   Type an empty line to quit.\n\n");

    run_chat_loop(agent);
    return 0;
}
