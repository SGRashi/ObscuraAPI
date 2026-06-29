#include <iostream>
#include <string>
#include <cstring>
#include <atomic>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pqxx/pqxx> // PostgreSQL C++ connector

// Maximum concurrent connections allowed
const int MAX_CONNECTIONS = 10;
std::atomic<int> active_conn{0};

void initialize_database(){
	try{
		const char* db_pass = std::getenv("DB_PASSWORD");
		if(db_pass == nullptr) {
			std::cout << "DB_PASSWORD env variable is not set\n";
			exit(1);
		}
		pqxx::connection conn{
				"host=127.0.0.1 port=5432 user=vault_admin "
				"password=" + std::string(db_pass) + " dbname=obscura_vault"};

		if(conn.is_open()) {
			std::cout << "[Database] Successfully connected to PostgreSQL container\n";
		}

			pqxx::work tx(conn);
			tx.exec("CREATE TABLE IF NOT EXISTS users("
				"id SERIAL PRIMARY KEY, "
				"username VARCHAR(50) UNIQUE NOT NULL, "
				"password_hash VARCHAR(255) NOT NULL, "
				"created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
				");"
			);
			tx.commit();
			std::cout << "[Database] Users table verified.\n";
	}
	catch (const std::exception &e) {
		std::cerr << "[Database Error]" << e.what() << "\n";
		exit(1);
	}
}

int main() {

	initialize_database();
    // 1. Create socket (IPv4, TCP)
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    // Set socket options to reuse the port instantly on restart
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. Set up address structures
    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    // 3. Bind the socket
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind Failed\n";
        close(server_fd);
        return 1;
    }

    // 4. Listen for connections (backlog of 10)
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen Failed\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Listening on port 8080...\n";

    // 5. Connection acceptance loop
    while (true) {
        struct sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        
        // Block and wait for a new connection
        int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
        if (client_fd < 0) {
            std::cerr << "Accept connection Failed\n";
            continue;
        }

        // Connection Limit Guard
        if (active_conn >= MAX_CONNECTIONS) {
            std::cerr << "[Warning] Too many connections. Rejecting client.\n";
            std::string overload_resp = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n";
            send(client_fd, overload_resp.c_str(), overload_resp.length(), 0);
            close(client_fd);
            continue;
        }

        active_conn++;
        std::cout << "[Server] Connections: " << active_conn.load() << "/10\n";

        // CONCURRENCY WITHOUT EXTERNAL FUNCTIONS:
        // We pass an anonymous lambda block directly into std::thread.
        // [client_fd] is captured by value so every thread gets its own socket copy.
        // [&active_conn] is captured by reference so we can safely decrement it when finished.
        std::thread([client_fd]() {
            char buffer[1024] = {0};
            
            // Read incoming HTTP request
            int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                std::string request(buffer);

		if(request.find("POST /register") != std::string::npos) {
		size_t body_pos = request.find("\r\n\r\n");
		if(body_pos != std::string::npos) {
		std::string body = request.substr(body_pos +4);

		std::cout << "[Server] Recieved Registration Data: " << body << "\n";                
		std::string response = "HTTP/1.1 200 OK\r\n"
		                       "Content-Length: 10\r\n"
				       "\r\n"
				       "Registered";

		send(client_fd, response.c_str(), response.length(), 0);

            }
	    }
	    else if(request.find("GET /") != std::string::npos) {
	    std::string response = "HTTP/1.1 200 OK\r\n"
	                           "Content-Length: 22\r\n"
				   "\r\n"
				   "Welcome to the Vault.";

            // Send response back
            send(client_fd, response.c_str(), response.length(), 0);
	    }
		}

            // Close connection
            close(client_fd);

            // Decrement the atomic counter
            active_conn--;        }).detach(); // Detach allows thread resources to auto-cleanup upon finishing
    }

    close(server_fd);
    return 0;
}

