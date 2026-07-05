#include <iostream>
#include <string>
#include <cstring>
#include <atomic>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pqxx/pqxx> // PostgreSQL C++ connector
#include <nlohmann/json.hpp>
#include "../../internal/storage/minio_client.hpp"
using json = nlohmann::json;

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
			tx.exec("CREATE TABLE IF NOT EXISTS files("
					"id SERIAL PRIMARY KEY, "
					"user_id INTEGER REFERENCES users(id), "
					"filename VARCHAR(255) NOT NULL, "
					"minio_key VARCHAR(255) NOT NULL, "
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

void handle_client(int client_fd) {
	char buffer[1024] = {0};
            
            // Read incoming HTTP request
            int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                std::string request(buffer);

		if(request.find("POST /register") != std::string::npos) {
		size_t body_pos = request.find("\r\n\r\n");
		if(body_pos != std::string::npos) {
		std::string body = request.substr(body_pos +4);

		try {
		   json req_json = json::parse(body);
		   std::string user = req_json["username"];
		   std::string pass = req_json["password"]; // this is what is needed to be hashed
		   const char* dp_pass = std::getenv("DB_PASSWORD");
		   std::string conn_str = "host=127.0.0.1 port=5432 user=vault_admin password="+std::string(dp_pass)+" dbname=obscura_vault";
		   pqxx::connection conn{conn_str};

		   pqxx::work tx{conn};
		   tx.exec(
				   "INSERT INTO users (username, password_hash) VALUES ($1, $2)",
				  pqxx::params{user, pass}
				 );
		   tx.commit();

		std::cout << "[Server] Registration Recieved Data: " << body << "\n";                
		std::string response = "HTTP/1.1 200 OK\r\n"
		                      "Content-Length: 10\r\n"
				      "\r\n"
				      "Registered";

		send(client_fd, response.c_str(), response.length(), 0);
		}
		catch (const std::exception& e) {
			std::cerr << "[Registration Error] " << e.what() << "\n";
			std::string err_response = "HTTP/1.1 400 Bad Request\r\n"
				                  "Content-Length: 15\r\n"
						  "\r\n"
						  "Request Failed\n";
			send(client_fd, err_response.c_str(), err_response.length(), 0);
		}
            }
	}

	   else if(request.find("POST /upload") != std::string::npos) {
		   size_t body_pos = request.find("\r\n\r\n");
		   if(body_pos != std::string::npos) {
			   std::string file_content = request.substr(body_pos + 4);
                           json req = json::parse(file_content);
			   std::string original_filename = req["filename"];
			   std::string data = req["content"];


                           std::string minio_key = "FILE_CREATED_AT_"+std::to_string(time(nullptr))+".bin";

			   MinioClient storage("http://localhost:9000", "minioadmin", "minioadmin", "obscura-api");
  
			   if(storage.upload_file(minio_key, data)) {
				   try {
					   const char* db_pass = std::getenv("DB_PASSWORD");
					   pqxx::connection conn{"host=127.0.0.1 port=5432 user=vault_admin password=" + std::string(db_pass) + " dbname=obscura_vault"};
					   pqxx::work tx{conn};

					  tx.exec( "INSERT INTO files (user_id, filename, minio_key) VALUES ($1, $2, $3)", 
                                                   pqxx::params{1, original_filename, minio_key}
);
					   tx.commit();

					   std::string response = "HTTP/1.1 200 OK\r\n"
						   "Content-Length: 14\r\n"
						   "\r\n"
						   "Upload Success";
					   send(client_fd, response.c_str(), response.length(), 0);
				   }
				   catch(const std::exception& e) {
					   std::cerr << "[DB Error] " << e.what() << std::endl;
				   }
			   }
		   }
	   }

		else if(request.find("POST /login") != std::string::npos) {
			size_t body_pos = request.find("\r\n\r\n");
			if(body_pos != std::string::npos)
			{
				std::string body = request.substr(body_pos + 4);
			

			try {
				json json_body = json::parse(body);
				std::string user = json_body["username"];
				std::string pass = json_body["password"];

				const char* db_pass = std::getenv("DB_PASSWORD");
				std::string conn_str = "host=127.0.0.1 port=5432 user=vault_admin password="+std::string(db_pass)+" dbname=obscura_vault";
				pqxx::connection conn{conn_str};
				pqxx::work tx{conn};

				pqxx::result res = tx.exec(
						"SELECT id FROM users WHERE username = $1 AND password_hash = $2",
						pqxx::params{user, pass}
						);
				tx.commit();

				if(!res.empty()) {
					int user_id = res[0][0].as<int>();
					std::cout << "[Server] User " + user + " logged in successfullly with ID: " << user_id << "\n";

					json response_json;
					response_json["status"] = "success";
					response_json["user_id"] = user_id;
					std::string response_body = response_json.dump();

					std::string response = "HTTP/1.1 200 OK\r\n"
						"Content-Type: application/json\r\n"
						"Content-Length: " + std::to_string(response_body.length()) + "\r\n"
						"\r\n" + response_body;

					send(client_fd, response.c_str(), response.length(), 0);
				}
				else {
					std::cout << "[Server] User " +user+ " is not Registered\n";
					std::string err_response = "HTTP/1.1 401 Unauthorized\r\n"
						"Content-Type: application/json\r\n"
						"Content-Length: 19\r\n"
						"\r\n"
						"Invalid credentials";
					send(client_fd, err_response.c_str(), err_response.length(), 0);
				}
			}
			catch(const std::exception& e) {
				std::cerr << "[LOGIN ERROR!] " << e.what() << "\n";
				std::string err_response = "HTTP/1.1 400 Bad Request\r\n";
					send(client_fd, err_response.c_str(), err_response.length(), 0);
			}
			}
		}
		else if(request.find("GET /download/") != std::string::npos) {
			try {
				size_t start_pos = request.find("GET /download/") + 14;
				size_t end_pos = request.find(" HTTP/");
				std::string file_id_str = request.substr(start_pos, end_pos - start_pos);
				int file_id = stoi(file_id_str);

				const char* db_pass = std::getenv("DB_PASSWORD");
				std::string conn_str = "host=127.0.0.1 port=5432 user=vault_admin password=" + std::string(db_pass) + " dbname=obscura_vault";
				pqxx::connection conn{conn_str};
				pqxx::work tx{conn};

				pqxx::result res = tx.exec(
						"SELECT filename, minio_key FROM files WHERE id = $1",
						pqxx::params{file_id}
						);
				tx.commit();

				if(!res.empty())
				{
					std::string filename = res[0][0].as<std::string>();
					std::string minio_key = res[0][1].as<std::string>();
					std::cout << "[Server] Downloading file: " << filename << " with Key: " + minio_key << "\n";

					MinioClient storage("http://localhost:9000", "minioadmin", "minioadmin", "obscura-api");
					std::string file_data = storage.download_file(minio_key);

					if(!file_data.empty()) {
						std::string header = "HTTP/1.1 200 OK\r\n"
							"Content-Type: application/octet-stream\r\n"
							"Content-Disposition: attachment; filename=\"" + filename + "\"\r\n"
							"Content-Length: " + std::to_string(file_data.length()) + "\r\n"
							"\r\n";

						send(client_fd, header.c_str(), header.length(), 0);
						send(client_fd, file_data.c_str(), file_data.length(), 0);

						std::cout << "[Server] Successfully send " << file_data.length() << " bytes.\n";
					}
					else {
						std::string err = "HTTP/1.1 500 Internal server error\r\n"
							"\r\n"
							"Failed to fetch the file from MinIO";
						send(client_fd, err.c_str(), err.length(), 0);
					}

				}
				else {
					std::string err = "HTTP/1.1 403 Not Found\r\n"
						"\r\n"
						"File not found in database";
					send(client_fd, err.c_str(), err.length(), 0);
				}
			}
			catch(const std::exception& e) {
				std::cerr << "[Download Error] " << e.what() << "\n";
				std::string err = "HTTP/1.1 400 Bad Request\r\n\r\n";
				send(client_fd, err.c_str(), err.length(), 0);
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

		else {
			std::string response = "HTTP/1.1 404 Not Found\r\n"
				               "\r\n"
					       "The Request cannot be identified";
			send(client_fd, response.c_str(), response.length(), 0);
		}
	}

            // Close connection
            close(client_fd);

            // Decrement the atomic counter
            active_conn--;
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
        std::thread(handle_client, client_fd).detach();
    }
            
    close(server_fd);
    return 0;
}

