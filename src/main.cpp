#include "ProxyServer.hpp"
#include <exception>
#include <iostream>

int main(int argc, char *argv[]) {
	try {
		ProxyServer proxy_server(argc, argv);
		AsyncLogger logger("resources/logs.txt");
		Parser parser(&logger);
		proxy_server.attachParser(&parser);
		proxy_server.run();
	} catch (const std::exception &e) {
		std::cerr << "Ошибка: " << e.what() << std::endl;
	}

	return 0;
}
