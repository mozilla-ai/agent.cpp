#pragma once

#include "agent.h"
#include "chat.h"
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#else
#include <unistd.h>
#endif // _WIN32

// Run an interactive chat loop with the given agent.
// Reads user input from stdin and prints agent responses to stdout.
// The loop continues until the user enters an empty line.
// Shared across examples to avoid code duplication.
inline void
run_chat_loop(agent_cpp::Agent& agent)
{
    std::vector<common_chat_msg> messages;

    while (true) {
        if (isatty(fileno(stdout))) {
            printf("\033[32m> \033[0m");
        } else {
            printf("> ");
        }
        std::string user_input;
        std::getline(std::cin, user_input);

        if (user_input.empty()) {
            break;
        }

        common_chat_msg user_msg;
        user_msg.role = "user";
        user_msg.content = user_input;
        messages.push_back(user_msg);

        agent.run_loop(messages, [](const std::string& chunk) {
            if (isatty(fileno(stdout))) {
                printf("\033[33m%s\033[0m", chunk.c_str());
            } else {
                printf("%s", chunk.c_str());
            }
            fflush(stdout);
        });
        printf("\n");
    }

    printf("\n👋 Goodbye!\n");
}

#ifdef _WIN32
#undef isatty
#endif // _WIN32
