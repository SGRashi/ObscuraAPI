#pragma once
#include <pqxx/pqxx>
#include <memory>
#include <string>
#include <iostream>
#include "../../cmd/server/config.hpp"

//Handles all the database related work
namespace Database {
    class DBManager {
        private:
        //This keeps the connection alive for the entire lifespan of the server
        std::unique_ptr<pqxx::connection> conn;

        public:
        DBManager() = default;

        //Initialization of Database
        void init(const Config& cfg) {
            try {
                std::string conn_str = "host=127.0.0.1 port=5432 user=vault_admin password=" + cfg.db_pass + " dbname=obscura_vault";

                conn = std::make_unique<pqxx::connection>(conn_str);

                if(conn->is_open()) {
                    std::cout << "[Database] Successfully connected to PostgreSQL container\n";
                }

                pqxx::work tx(*conn);
                tx.exec("CREATE TABLE IF NOT EXISTS users("
                        "id SERIAL PRIMARY KEY, "
                        "username VARCHAR(50) UNIQUE NOT NULL, "
                        "password_hash VARCHAR(255) NOT NULL, "
                        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                        ");");

                tx.exec("CREATE TABLE IF NOT EXISTS files("
                        "id SERIAL PRIMARY KEY, "
                        "user_id INTEGER REFERENCES users(id), "
                        "filename VARCHAR(255) NOT NULL, "
                        "minio_key VARCHAR(255) NOT NULL, "
                        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                        ");");
                tx.commit();

                std::cout << "[Database] Users and Files tables verified.\n";
            }
            catch (const std::exception& e) {
                std::cerr << "[Database Error]" << e.what() << "\n";
                exit(1); 
            }
        }

        //registration route
        bool create_user(const std::string& username, std::string& password_hash) {
            try {
                pqxx::work W(*conn);
                std::string query = "INSERT INTO users (username, password_hash) VALUES ($1, $2);";
                W.exec(query, pqxx::params{username, password_hash});
                W.commit();
                return true;
            }
            catch (const std::exception& e) { 
                std::cerr << "[Database Error] Registration failed for " << username << ": " << e.what() << "\n";
                return false;
            }
        }

        //login route
        bool get_user_auth(const std::string& username, std::string& hash_password, int& user_id) {
            try {
                pqxx::work W(*conn);
                std::string query = "SELECT id, password_hash FROM users WHERE username = $1;";
                pqxx:: result r = W.exec(query, pqxx::params{username});

                if(r.empty())
                return false;

                user_id = r[0]["id"].as<int>();
                hash_password = r[0]["password_hash"].as<std::string>();
                return true;
            }
            catch (const std::exception& e) {
                std::cerr << "[Database Error] User authentication failed: " << e.what() << "\n";
                return false;
            }
        }

        //upload route
        bool file_upload(int user_id, const std::string& filename, const std::string& minio_key) {
            try {
                pqxx::work W(*conn);
                std::string query = "INSERT INTO files (user_id, filename, minio_key) VALUES ($1, $2, $3);";
                W.exec(query, pqxx::params{user_id, filename, minio_key});
                W.commit();
                return true;
            }
            catch(const std::exception& e) {
                std::cerr << "[Database Error] Failed to upload the file for user " << user_id << ": " << e.what() << "\n";
                return false;
            }
        }

        //download route
        bool get_file_metadata(int file_id, int authenticated_user_id, std::string& filename, std::string& minio_key) {
            try {
                pqxx::work W(*conn);
                std::string query = "SELECT filename, minio_key FROM files WHERE id = $1 AND user_id = $2;";
                pqxx::result r = W.exec(query, pqxx::params{file_id, authenticated_user_id});

                if(r.empty())
                return false;

                filename = r[0]["filename"].as<std::string>();
                minio_key = r[0]["minio_key"].as<std::string>();
                return true;
            }
            catch(const std::exception& e) {
                std::cerr << "[Database Error] Failed to fetch metadata of the file" << file_id << ": " << e.what() << "\n";
                return false;
            }
        }
    };
}

