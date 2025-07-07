#include "Parser.hpp"
#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

Parser::Parser(AsyncLogger *logger) : logger(logger), prepared_statements() {
	if (logger == nullptr) {
		throw std::invalid_argument("Logger is nullptr");
	}
}

bool Parser::parseClientMessage(const char *data, size_t len) {
	bool logged = false;
	size_t offset = 0;

	while (offset + 5 <= len) {
		char type = data[offset];
		uint32_t msg_len =
			ntohl(*reinterpret_cast<const uint32_t *>(&data[offset + 1]));

		if (offset + 1 + msg_len > len)
			break;

		const char *msg = data + offset + 5;
		size_t msg_size = msg_len - 4;

		switch (type) {
		case 'Q':
			logged |= parseQ(msg, msg_size);
			break;
		case 'P':
			logged |= parseP(msg, msg_size);
			break;
		case 'E':
			logged |= parseE(msg, msg_size);
			break;
		case 'B':
			logged |= parseB(msg, msg_size);
			break;
		case 'S':
			logged |= parseS(msg, msg_size);
			break;
		case 'X':
			logged |= parseX(msg, msg_size);
			break;
		case 'C':
			logged |= parseC(msg, msg_size);
			break;
		case 'D':
			logged |= parseD(msg, msg_size);
			break;
		case 'H':
			logged |= parseH(msg, msg_size);
			break;
		case 'F':
			logged |= parseF(msg, msg_size);
			break;
		default:
			break;
		}

		offset += 1 + msg_len;
	}

	return logged;
}

void Parser::logQuery(const std::string &query) { logger->log(query); }

bool Parser::parseQ(const char *data, size_t len) {
	std::string query(data, strnlen(data, len));
	if (!query.empty()) {
		logQuery("[QUERY] " + query);
		return true;
	}
	return false;
}

bool Parser::parseP(const char *data, size_t len) {
	size_t stmt_len = strnlen(data, len);
	if (stmt_len >= len)
		return false;

	std::string statement_name(data, stmt_len);
	const char *query_start = data + stmt_len + 1;
	size_t query_len = strnlen(query_start, len - stmt_len - 1);
	if (stmt_len + 1 + query_len > len)
		return false;

	std::string query(query_start, query_len);
	if (!query.empty()) {
		prepared_statements[statement_name] = query;
		logQuery("[PREPARE] " + statement_name + ": " + query);
		return true;
	}
	return false;
}

bool Parser::parseE(const char *data, size_t len) {
	std::string portal(data, strnlen(data, len));
	auto stmt_it = portal_to_statement.find(portal);
	if (stmt_it != portal_to_statement.end()) {
		const std::string &stmt_name = stmt_it->second;
		auto prep_it = prepared_statements.find(stmt_name);
		if (prep_it != prepared_statements.end()) {
			logQuery("[EXECUTE] " + portal + " → " + stmt_name + ": " +
					 prep_it->second);
		} else {
			logQuery("[EXECUTE] " + portal + " → unknown statement: '" +
					 stmt_name + "'");
		}
	} else {
		logQuery("[EXECUTE] unknown portal: '" + portal + "'");
	}
	return true;
}

bool Parser::parseB(const char *data, size_t len) {
	size_t portal_len = strnlen(data, len);
	if (portal_len >= len)
		return false;

	const char *stmt_ptr = data + portal_len + 1;
	size_t stmt_len = strnlen(stmt_ptr, len - portal_len - 1);
	if (portal_len + 1 + stmt_len > len)
		return false;

	std::string portal(data, portal_len);
	std::string stmt(stmt_ptr, stmt_len);
	portal_to_statement[portal] = stmt;

	std::string params;
	const char *params_ptr = stmt_ptr + stmt_len + 1;
	if (portal_len + 1 + stmt_len + 1 < len) {
		params =
			parseParameters(params_ptr, len - (portal_len + 1 + stmt_len + 1));
	}

	//logQuery("[BIND] " + portal + " → " + stmt +
	//		 (params.empty() ? "" : " (" + params + ")"));
	return true;
}

std::string Parser::parseParameters(const char *data, size_t len) {
	if (len < 2)
		return "";

	uint16_t num_params = ntohs(*reinterpret_cast<const uint16_t *>(data));
	std::ostringstream oss;

	const char *ptr = data + 2;
	for (uint16_t i = 0;
		 i < num_params && (ptr - data) < static_cast<ptrdiff_t>(len); ++i) {
		if (i > 0)
			oss << ", ";
		if (*ptr == '\x01') {
			oss << "TEXT:";
			ptr++;
			uint32_t param_len =
				ntohl(*reinterpret_cast<const uint32_t *>(ptr));
			ptr += 4;
			if ((ptr - data) + param_len > static_cast<ptrdiff_t>(len))
				break;
			oss << std::string(ptr, param_len);
			ptr += param_len;
		} else if (*ptr == '\x00') {
			oss << "BINARY:";
			ptr++;
			uint32_t param_len =
				ntohl(*reinterpret_cast<const uint32_t *>(ptr));
			ptr += 4;
			if ((ptr - data) + param_len > static_cast<ptrdiff_t>(len))
				break;
			oss << "[binary " << param_len << " bytes]";
			ptr += param_len;
		} else {
			break;
		}
	}
	return oss.str();
}

bool Parser::parseS(const char *data, size_t len) {
	//	logQuery("[SYNC]");
	return true;
}

bool Parser::parseX(const char *data, size_t len) {
	//logQuery("[TERMINATE]");
	return true;
}

bool Parser::parseC(const char *data, size_t len) {
	if (len < 1)
		return false;

	char close_type = data[0];
	std::string name(data + 1, strnlen(data + 1, len - 1));

	switch (close_type) {
	case 'S':
		//logQuery("[CLOSE STATEMENT] " + name);
		break;
	case 'P':
		//logQuery("[CLOSE PORTAL] " + name);
		break;
	default:
		break;
	}
	return true;
}

bool Parser::parseD(const char *data, size_t len) {
	if (len < 1)
		return false;

	char desc_type = data[0];
	std::string name(data + 1, strnlen(data + 1, len - 1));

	switch (desc_type) {
	case 'S':
		//logQuery("[DESCRIBE STATEMENT] " + name);
		break;
	case 'P':
		//logQuery("[DESCRIBE PORTAL] " + name);
		break;
	default:
		break;
	}
	return true;
}

bool Parser::parseH(const char *data, size_t len) {
	//logQuery("[FLUSH]");
	return true;
}

bool Parser::parseF(const char *data, size_t len) {
	///logQuery("[FUNCTION CALL] (legacy)");
	return true;
}