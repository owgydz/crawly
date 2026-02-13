// crawly crawler crawl
#include <iostream>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <gumbo.h>
#include "cxxopts.hpp"

#define CRAWLY_VERSION "0.0.1"

std::mutex queue_mutex;
std::mutex visited_mutex;

struct UrlNode {
    std::string url;
    int depth;
};

class RobotsTxt {
public:
    std::unordered_set<std::string> disallowed_paths;

    RobotsTxt() = default;

    void fetch_and_parse(const std::string& base_url) {
        std::string robots_url = base_url + "/robots.txt";
        CURL* curl = curl_easy_init();
        std::string readBuffer;

        if(curl) {
            curl_easy_setopt(curl, CURLOPT_URL, robots_url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            curl_easy_perform(curl);
            curl_easy_cleanup(curl);
        }

        std::istringstream ss(readBuffer);
        std::string line;
        while(std::getline(ss, line)) {
            if(line.find("Disallow:") != std::string::npos) {
                std::string path = line.substr(9); // remove "Disallow:"
                path.erase(0, path.find_first_not_of(" \t"));
                if(!path.empty()) disallowed_paths.insert(path);
            }
        }
    }

    bool is_allowed(const std::string& url, const std::string& domain) {
        std::string path = url.substr(url.find(domain) + domain.length());
        for(auto& dis : disallowed_paths) {
            if(path.find(dis) == 0) return false;
        }
        return true;
    }

private:
    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
};

class Crawly {
public:
    std::queue<UrlNode> url_queue;
    std::unordered_set<std::string> visited;
    nlohmann::json results;

    int max_pages;
    int max_depth;
    int delay_ms;
    bool verbose;
    std::string domain;
    RobotsTxt robots;

    Crawly(const std::string& start_url, int max_pages_, int max_depth_, int delay_ms_, bool verbose_, const std::string& domain_)
        : max_pages(max_pages_), max_depth(max_depth_), delay_ms(delay_ms_), verbose(verbose_), domain(domain_)
    {
        url_queue.push({start_url, 0});
        robots.fetch_and_parse("https://" + domain);
    }

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    std::string fetch_page(const std::string& url) {
        if(verbose) std::cout << "Fetching: " << url << std::endl;
        CURL* curl = curl_easy_init();
        std::string readBuffer;
        if(curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            curl_easy_perform(curl);
            curl_easy_cleanup(curl);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        return readBuffer;
    }

    void extract_links_recursive(GumboNode* node, std::vector<std::string>& links) {
        if(node->type != GUMBO_NODE_ELEMENT) return;

        if(node->v.element.tag == GUMBO_TAG_A) {
            GumboAttribute* href = gumbo_get_attribute(&node->v.element.attributes, "href");
            if(href) {
                std::string link = href->value;
                if(link.find("http://") == 0 || link.find("https://") == 0) {
                    if(domain.empty() || link.find(domain) != std::string::npos)
                        links.push_back(link);
                }
            }
        }

        GumboVector* children = &node->v.element.children;
        for(unsigned int i = 0; i < children->length; ++i)
            extract_links_recursive(static_cast<GumboNode*>(children->data[i]), links);
    }

    std::vector<std::string> parse_links(const std::string& html) {
        std::vector<std::string> links;
        GumboOutput* output = gumbo_parse(html.c_str());
        extract_links_recursive(output->root, links);
        gumbo_destroy_output(&kGumboDefaultOptions, output);
        return links;
    }

    void crawl() {
        while(true) {
            UrlNode node;
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if(url_queue.empty() || visited.size() >= max_pages) break;
                node = url_queue.front();
                url_queue.pop();
            }

            {
                std::lock_guard<std::mutex> lock(visited_mutex);
                if(visited.count(node.url)) continue;
                visited.insert(node.url);
            }

            if(node.depth > max_depth) continue;
            if(!robots.is_allowed(node.url, domain)) {
                if(verbose) std::cout << "Blocked by robots.txt: " << node.url << std::endl;
                continue;
            }

            std::string html = fetch_page(node.url);
            if(html.empty()) continue;

            std::vector<std::string> links = parse_links(html);
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                for(auto& link : links) {
                    if(!visited.count(link)) url_queue.push({link, node.depth + 1});
                }
            }

            results[node.url] = { {"links_found", links.size()}, {"depth", node.depth} };
            if(verbose) std::cout << "Crawled: " << node.url << " (" << links.size() << " links)" << std::endl;
        }
    }
};

int main(int argc, char** argv) {
    cxxopts::Options options("Crawly", "Web crawler v0.0.1");

    options.add_options()
        ("u,start-url", "Start URL", cxxopts::value<std::string>())
        ("m,max-pages", "Max pages to crawl", cxxopts::value<int>()->default_value("50"))
        ("d,max-depth", "Max crawl depth", cxxopts::value<int>()->default_value("3"))
        ("t,threads", "Number of threads", cxxopts::value<int>()->default_value("4"))
        ("o,output", "Output JSON file", cxxopts::value<std::string>()->default_value("crawly_results.json"))
        ("delay", "Delay between requests (ms)", cxxopts::value<int>()->default_value("200"))
        ("verbose", "Verbose output")
        ("v,version", "Show version")
        ("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if(result.count("version")) {
        std::cout << "Crawly version " << CRAWLY_VERSION << std::endl;
        return 0;
    }

    if(result.count("help") || !result.count("start-url")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    std::string start_url = result["start-url"].as<std::string>();
    int max_pages = result["max-pages"].as<int>();
    int max_depth = result["max-depth"].as<int>();
    int threads_count = result["threads"].as<int>();
    int delay_ms = result["delay"].as<int>();
    bool verbose = result.count("verbose") > 0;

    // Extract domain
    std::string domain = start_url;
    if(auto pos = domain.find("://"); pos != std::string::npos) domain = domain.substr(pos+3);
    if(auto slash = domain.find("/"); slash != std::string::npos) domain = domain.substr(0, slash);

    Crawly crawler(start_url, max_pages, max_depth, delay_ms, verbose, domain);

    std::vector<std::thread> threads;
    for(int i = 0; i < threads_count; ++i)
        threads.emplace_back([&crawler]() { crawler.crawl(); });

    for(auto& t : threads) t.join();

    std::ofstream ofs(result["output"].as<std::string>());
    ofs << crawler.results.dump(4);
    ofs.close();

    std::cout << "Crawling complete. Results saved to " << result["output"].as<std::string>() << std::endl;
    return 0;
}
