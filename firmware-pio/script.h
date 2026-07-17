#ifndef FIRMWARE_PIO_SCRIPT_H_
#define FIRMWARE_PIO_SCRIPT_H_

#include <string>
#include <string_view>
#include <vector>
#include <cctype>

namespace script {

using errstring = std::string;

using CommandFunction = errstring (*)(const std::vector<std::string>& argv);

struct Command {
    std::string name;
    CommandFunction func;
};

inline errstring Eval(std::string_view script_text, const std::vector<Command>& commands) {
    size_t i = 0;
    while (i < script_text.length()) {
        std::vector<std::string> words;
        
        // Parse a single command
        while (i < script_text.length()) {
            char c = script_text[i];
            
            // Semicolon or newline terminates the command
            if (c == ';' || c == '\n') {
                i++;
                break;
            }
            
            // Skip other whitespace
            if (std::isspace(static_cast<unsigned char>(c))) {
                i++;
                continue;
            }
            
            // Comment: '#' at the start of a word ignores the rest of the command
            if (c == '#') {
                while (i < script_text.length() && script_text[i] != ';' && script_text[i] != '\n') {
                    i++;
                }
                continue;
            }
            
            // Read a word
            size_t start = i;
            while (i < script_text.length()) {
                c = script_text[i];
                if (std::isspace(static_cast<unsigned char>(c)) || c == ';') {
                    break;
                }
                i++;
            }
            words.push_back(std::string(script_text.substr(start, i - start)));
        }
        
        // Execute the command if it's not empty
        if (!words.empty()) {
            bool found = false;
            for (const auto& cmd : commands) {
                if (cmd.name == words[0]) {
                    found = true;
                    errstring err = cmd.func(words);
                    if (!err.empty()) {
                        return err;
                    }
                    break;
                }
            }
            if (!found) {
                return "Command not found: " + words[0];
            }
        }
    }
    return "";
}

extern std::vector<Command> global_script_commands;

} // namespace script

#endif // FIRMWARE_PIO_SCRIPT_H_
