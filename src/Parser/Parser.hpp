#pragma once
#include <string>
#include <unordered_map>

#include "AsyncLogger.hpp"

class Parser {
    public:
        Parser(AsyncLogger * logger);
        std::string parse(const char* data, size_t len);
        bool parseClientMessage(const char * data, size_t len);

    private:
        AsyncLogger * logger;


        std::unordered_map<std::string, std::string> prepared_statements;
        std::unordered_map<std::string, std::string> portal_to_statement;

        void logQuery(const std::string& query);

        bool parseQ(const char* data, size_t len);
        bool parseP(const char* data, size_t len);
        bool parseE(const char* data, size_t len);
        bool parseB(const char* data, size_t len);
        bool parseS(const char* data, size_t len);
        bool parseX(const char* data, size_t len);
        bool parseC(const char* data, size_t len);
        bool parseD(const char* data, size_t len);
        bool parseH(const char* data, size_t len);
        bool parseF(const char* data, size_t len);
        std::string parseParameters(const char* data, size_t len);
};