#include <iostream>
#include <string>
#include <cstring>
#include <atomic>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <jwt-cpp/jwt.h>
#include "config.hpp"
#include "../../internal/storage/minio_client.hpp"
#include "../../internal/encryption/crypto_utils.hpp"
#include "../../internal/database/db.hpp"
using json = nlohmann::json;

const int MAX_CONNECTIONS = 10;
std::atomic<int> active_conn{0};
Config cfg;
Database::DBManager db;

void handle_client(int client_fd) {
	char buffer[1024] = {0};
            
    // Read incoming HTTP request
	 int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
     if (bytes_read > 0) {
     std::string request(buffer);


	 //<=================>
	 //REGISTER ROUTE
	 //<=================>
		if(request.find("POST /register") != std::string::npos) {
		size_t body_pos = request.find("\r\n\r\n");
		if(body_pos != std::string::npos) {
		std::string body = request.substr(body_pos +4);

		try {
		   json req_json = json::parse(body);
		   std::string user = req_json["username"];
		   std::string pass = req_json["password"];
		   std::string hashed_password = Crypto::hash_password(pass);

		   //Creating user
		   if(db.create_user(user, hashed_password)) {
		   std::cout << "[Server] Registration Recieved Data: " << body << "\n";                
		   std::string response = "HTTP/1.1 200 OK\r\n"
		                      "Content-Length: 28\r\n"
				              "\r\n"
				              "User Registered Successfully";

		   send(client_fd, response.c_str(), response.length(), 0);
		   }
		   else {
			throw std::runtime_error("Username already exists or SQL failure");
		   }
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
    //<=================>
	//UPLOAD ROUTE
	//<=================>
	   else if(request.find("POST /upload") != std::string::npos) {
		   size_t body_pos = request.find("\r\n\r\n");
		   if(body_pos != std::string::npos) {
			   std::string file_content = request.substr(body_pos + 4);
                           json req = json::parse(file_content);
			   std::string original_filename = req["filename"];
			   std::string data = req["content"];


               std::string minio_key = "FILE_CREATED_AT_"+std::to_string(time(nullptr))+".bin";

			   MinioClient storage("http://localhost:9000", cfg.minio_ak, cfg.minio_sk, "obscura-api");
  
			   if(storage.upload_file(minio_key, data)) {
				   if(db.file_upload(1, original_filename, minio_key)) {
					    std::string response = "HTTP/1.1 200 OK\r\n"
                                               "Content-Length: 14\r\n"
                                               "\r\n"
                                               "Upload Success";
						send(client_fd, response.c_str(), response.length(), 0);
				   }
				   else {
					    std::cerr << "[Critical Error] MinIO upload succeeded but DB metadata tracking failed!\n";
                        
                        std::string err_body = "{\"status\":\"error\",\"message\":\"Database tracking failed\"}";
                        std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
                                               "Content-Type: application/json\r\n"
                                               "Content-Length: " + std::to_string(err_body.length()) + "\r\n"
                                               "\r\n" + err_body;
                        send(client_fd, response.c_str(), response.length(), 0);
				   }
			   }
			   else {
				    std::string err_body = "{\"status\":\"error\",\"message\":\"Storage server rejected upload\"}";
                    std::string response = "HTTP/1.1 502 Bad Gateway\r\n"
                                           "Content-Type: application/json\r\n"
                                           "Content-Length: " + std::to_string(err_body.length()) + "\r\n"
                                           "\r\n" + err_body;
                    send(client_fd, response.c_str(), response.length(), 0);
			   }
		   }
	   }
        //<==================>
		// LOGIN ROUTE 
		//<==================>
		else if(request.find("POST /login") != std::string::npos) {
			size_t body_pos = request.find("\r\n\r\n");
			if(body_pos != std::string::npos) {
				std::string body = request.substr(body_pos + 4);
		
			try {
				json json_body = json::parse(body);
				std::string user = json_body["username"];
				std::string pass = json_body["password"];

				std::string stored_hash;
				int user_id = 0;

				if(db.get_user_auth(user, stored_hash, user_id)) {
					if(Crypto::verify_password(stored_hash, pass)) {

					std::cout << "[Server] User " + user + " logged in successfullly with ID: " << user_id << "\n";
                    
					//Generating the JWT
					auto token = jwt::create()
					  .set_issuer("obscura-api")
					  .set_type("JWS")
					  .set_payload_claim("user_id", jwt::claim(std::to_string(user_id)))
					  .set_issued_at(std::chrono::system_clock::now())
					  .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours{24})
					  .sign(jwt::algorithm::hs256{cfg.jwt_secret});
                    //End of token generation

					json response_json;
					response_json["status"] = "success";
					response_json["user_id"] = user_id;
					response_json["token"] = token;
					std::string response_body = response_json.dump();

					std::string response = "HTTP/1.1 200 OK\r\n"
						"Content-Type: application/json\r\n"
						"Content-Length: " + std::to_string(response_body.length()) + "\r\n"
						"\r\n" + response_body;

					send(client_fd, response.c_str(), response.length(), 0);
					}
				else {
						std::cout << "[Server] Invalid password attempt for: " << user << "\n";
						std::string response = "HTTP/1.1 401 Unauthorized\r\n"
							               "Content-Type: application/json\r\n"
								       "Content-Length: 16\r\n"
								       "\r\n"
								       "Invalid password";
						send(client_fd, response.c_str(), response.length(), 0);
	                }
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

	    //<=================>
		//DOWNLOAD ROUTE
		//<=================>
		else if(request.find("GET /download/") != std::string::npos) {
			try {
				size_t start_pos = request.find("GET /download/") + 14;
				size_t end_pos = request.find(" HTTP/");
				std::string file_id_str = request.substr(start_pos, end_pos - start_pos);
				int file_id = stoi(file_id_str);

				std::string filename;
				std::string minio_key;

				if(db.get_file_metadata(file_id, 1, filename, minio_key)) {
					std::cout << "[Server] Downloading file: " << filename << " with Key: " + minio_key << "\n";

					MinioClient storage("http://localhost:9000", cfg.minio_ak, cfg.minio_sk, "obscura-api");
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
        //<====================>
		//DEFAULT FALLBACK
		//<====================>
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
	db.init(cfg);
    // 1. Create socket (IPv4, TCP)
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind Failed\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen Failed\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Listening on port 8080...\n";

    while (true) {
        struct sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
        if (client_fd < 0) {
            std::cerr << "Accept connection Failed\n";
            continue;
        }

        if (active_conn >= MAX_CONNECTIONS) {
            std::cerr << "[Warning] Too many connections. Rejecting client.\n";
            std::string overload_resp = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n";
            send(client_fd, overload_resp.c_str(), overload_resp.length(), 0);
            close(client_fd);
            continue;
        }

        active_conn++;
        std::cout << "[Server] Connections: " << active_conn.load() << "/10\n";

        std::thread(handle_client, client_fd).detach();
    }
            
    close(server_fd);
    return 0;
}

