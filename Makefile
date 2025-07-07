JSON_CONFIG	 = $(shell cat config.json)

PORT_PROXY_SERVER := $(shell jq -r ".port_proxy_server" config.json)
HOST_PG := $(shell jq -r ".host_pg" config.json)
PORT_PG := $(shell jq -r ".port_pg" config.json)
PG_SUPERUSER := $(shell jq -r ".pg_superuser" config.json)
PG_SUPERUSER_PASSWORD := $(shell jq -r ".pg_superuser_password" config.json)


SYSBENCH_USER ?= sbuser
SYSBENCH_PASSWORD ?= sysbench
SYSBENCH_DB ?= sbtest

SYSBENCH_THREADS := $(shell jq -r ".sysbench_threads" config.json)
SYSBENCH_TIME := $(shell jq -r ".sysbench_time" config.json)
SYSBENCH_TABLES := $(shell jq -r ".sysbench_tables" config.json)
SYSBENCH_TABLE_SIZE := $(shell jq -r ".sysbench_table_size" config.json)


BUILD_LOG_FILE = build.log
SYSBENCH_SCRIPT = /usr/share/sysbench/oltp_read_write.lua
PG_USER = sbuser
PG_PASSWORD = sysbench
PG_DB = sbtest

PSQL_CMD = PGPASSWORD=$(PG_SUPERUSER_PASSWORD) psql -h $(HOST_PG) -p $(PORT_PG) -U $(PG_SUPERUSER) -w

proxy_server:
	@mkdir -p build
	@mkdir -p resources
	@mkdir -p bin
	@echo "--> Сборка (логи в build.log)"
	@(cmake -S src/ -B build && make -C build) >build.log 2>&1 || { \
		echo "--> ОШИБКА"; \
		tail -n 20 build.log; \
		exit 1; \
	}
	@mv build/pg_proxy bin
	@echo "--> Успешно!"

run_server: proxy_server
	@echo "--> Запуск прокси-сервера на порту $(PORT_PROXY_SERVER)"
	@bin/pg_proxy $(PORT_PROXY_SERVER) $(HOST_PG) $(PORT_PG) & echo $$! > proxy.pid
	@echo "--> Сервер запущен (PID: $$(cat proxy.pid))"

stop_server:
	@if [ -f proxy.pid ]; then \
		kill $$(cat proxy.pid) && rm proxy.pid; \
		echo "--> Сервер остановлен"; \
	else \
		echo "--> Сервер не запущен"; \
	fi

clean:
	@rm -rf build bin resources/logs.txt resources/sysbench_result.txt $(BUILD_LOG_FILE) 2> /dev/null || true
	@echo "--> Очистка завершена"

sysbench_full_setup: sysbench_install db_user_create db_create sysbench_prepare
	@echo "--> Окружение sysbench готово!"


db_user_create:
	@echo "--> Создание пользователя базы данных $(SYSBENCH_USER)"
	@if ! $(PSQL_CMD) -c "SELECT 1 FROM pg_roles WHERE rolname='$(SYSBENCH_USER)'" -t | grep -q 1; then \
		$(PSQL_CMD) -c "CREATE USER $(SYSBENCH_USER) WITH PASSWORD '$(SYSBENCH_PASSWORD)';"; \
		echo "--> Пользователь создан"; \
	else \
		echo "--> Пользователь уже существует"; \
	fi

db_create:
	@echo "--> Создание базы данных $(SYSBENCH_DB)"
	@if ! $(PSQL_CMD) -c "SELECT 1 FROM pg_database WHERE datname='$(SYSBENCH_DB)'" -t | grep -q 1; then \
		$(PSQL_CMD) -c "CREATE DATABASE $(SYSBENCH_DB) OWNER $(SYSBENCH_USER);"; \
		echo "--> База данных создана"; \
	else \
		echo "--> База данных уже существует"; \
	fi
	@$(PSQL_CMD) -d $(SYSBENCH_DB) \
		-c "GRANT ALL PRIVILEGES ON DATABASE $(SYSBENCH_DB) TO $(SYSBENCH_USER);"

sysbench_install:
	@if ! command -v sysbench >/dev/null 2>&1; then \
		echo "Установка sysbench..."; \
		sudo apt-get update && sudo apt-get install -y sysbench; \
	else \
		echo "Sysbench уже установлена"; \
	fi

sysbench_clean:
	@echo "--> Удаление существующих sysbench таблиц..."
	@$(PSQL_CMD) -c "DROP TABLE IF EXISTS sbtest1, sbtest2, sbtest3, sbtest4 CASCADE" 2>/dev/null || true
	@echo "--> Таблицы удалены"

sysbench_full_clean:
	@echo "--> Полное удаление окружения sysbench..."
	@$(PSQL_CMD) \
		-c "DROP DATABASE IF EXISTS $(SYSBENCH_DB);" >/dev/null 2>&1 || true
	@$(PSQL_CMD) \
		-c "DROP USER IF EXISTS $(SYSBENCH_USER);" >/dev/null 2>&1 || true
	@echo "--> Очистка завершена"

sysbench_prepare: sysbench_install
	@echo "--> Подготовка sysbench к тестированию..."
	@sysbench $(SYSBENCH_SCRIPT) \
		--db-driver=pgsql \
		--pgsql-host=$(HOST_PG) \
		--pgsql-port=$(PORT_PG) \
		--pgsql-user=$(PG_USER) \
		--pgsql-password=$(PG_PASSWORD) \
		--pgsql-db=$(PG_DB) \
		--tables=$(SYSBENCH_TABLES) \
		--table-size=$(SYSBENCH_TABLE_SIZE) \
		prepare


sysbench_run: run_server
	@echo "--> Запущен sysbench тест на $(SYSBENCH_TIME) секунд с $(SYSBENCH_THREADS) потоками"
	@sysbench $(SYSBENCH_SCRIPT) \
		--db-driver=pgsql \
		--pgsql-host=127.0.0.1 \
		--pgsql-port=$(PORT_PROXY_SERVER) \
		--pgsql-user=$(PG_USER) \
		--pgsql-password=$(PG_PASSWORD) \
		--pgsql-db=$(PG_DB) \
		--tables=$(SYSBENCH_TABLES) \
		--threads=$(SYSBENCH_THREADS) \
		--time=$(SYSBENCH_TIME) \
		--report-interval=10 \
		run 2>&1 | tee resources/sysbench_result.txt
	@$(MAKE) --no-print-directory stop_server




help:
	@echo "\033[32mЦели:\033[0m"
	@echo "  \033[36mproxy_server\033[0m          --> \033[32mBuild прокси сервера\033[0m"
	@echo "  \033[36mrun_server\033[0m            --> \033[32mЗапуск прокси сервера в фоне\033[0m"
	@echo "  \033[36mstop_server\033[0m           --> \033[32mОстановка запущенного сервера\033[0m"
	@echo "  \033[36msysbench_full_setup\033[0m   --> \033[32mПодготовка окружения для теста sysbench\033[0m"
	@echo "  \033[36msysbench_run\033[0m          --> \033[32mЗапуск сервера в фоне и sysbench теста\033[0m"
	@echo "  \033[36msysbench_full_clean\033[0m   --> \033[32mПолная очистка окружения sysbench\033[0m"
	@echo "  \033[36mclean\033[0m                 --> \033[32mОчистка папок build bin resources\033[0m"

	