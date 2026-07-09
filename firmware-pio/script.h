#ifndef FIRMWARE_PIO_SCRIPT_H_
#define FIRMWARE_PIO_SCRIPT_H_

#include <string>
#include <string_view>
#include <vector>
#include <cctype>

namespace script {

using CommandFunction = void (*)(int argc, const char* const* argv);

struct Command {
    const char* name;
    CommandFunction func;
};

inline void Exec(std::string_view script_text, const std::vector<Command>& commands) {
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
            std::vector<const char*> argv;
            argv.reserve(words.size());
            for (const auto& w : words) {
                argv.push_back(w.c_str());
            }
            
            for (const auto& cmd : commands) {
                if (std::string_view(cmd.name) == words[0]) {
                    cmd.func(static_cast<int>(argv.size()), argv.data());
                    break;
                }
            }
        }
    }
}

} // namespace script

#endif // FIRMWARE_PIO_SCRIPT_H_
