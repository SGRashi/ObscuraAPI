#pragma once
#include <string>
#include <iostream>
#include <curl/curl.h>

class MinioClient {
private:
	std::string endpoint;
	std::string access_key;
	std::string secret_key;
	std::string bucket_name;

public:
	MinioClient(std::string ep, std::string ak, std::string sk, std::string bucket)
        : endpoint(ep), access_key(ak), secret_key(sk), bucket_name(bucket) 
    {

        std::cout << "[Storage] Client initialized for: " << endpoint << "/" << bucket_name << std::endl;
    }

	bool upload_file(const std::string& filename, const std::string& file_data){
		if(endpoint.empty()) {
			std::cerr << "[Storage] Error: Endpoint is empty!" << std::endl;
            return false;
		}
		CURL* curl = curl_easy_init();
		if(!curl) 
			return false;

		char errbuf[CURL_ERROR_SIZE];
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

		std::string url = endpoint + "/" + bucket_name + "/" + filename;

		struct curl_slist* headers = nullptr;
		headers = curl_slist_append(headers, "Content-Type: application/octet-stream");

		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, file_data.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)file_data.size());
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		CURLcode res = curl_easy_perform(curl);

		long http_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                if(res != CURLE_OK) {
			std::cerr << "[libcurl] Error: " << errbuf << std::endl;
		}
		std::cout << "[Storage] HTTP Status Code: " << http_code << std::endl;
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);

		return (res == CURLE_OK && http_code == 200);
	}

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t total_size = size * nmemb;
        std::string* str = static_cast<std::string*>(userp);
        str->append(static_cast<char*>(contents), total_size);
        return total_size;
    }

       std::string download_file(const std::string& object_name) {
        CURL* curl = curl_easy_init();
        std::string read_buffer; 
        if(curl) {
            std::string url = endpoint + "/" + bucket_name + "/" + object_name;
            
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);           

            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
            
            CURLcode res = curl_easy_perform(curl);
            
            if(res != CURLE_OK) {
                std::cerr << "[MinIO Error] Download failed: " << curl_easy_strerror(res) << "\n";
            }
            
            curl_easy_cleanup(curl);
        }
        return read_buffer;
    }
};
