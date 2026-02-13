# crawly

**Crawly** is a simple web-crawler i made

## Features
- Multi-threaded crawling
- `robots.txt` support
- Crawl depth control
- Polite delay between requests
- Domain-restricted crawling
- JSON output
- CLI options with `--verbose` and `--version`

## Requirements
- C++17 compatible compiler
- [libcurl](https://curl.se/libcurl/)
- [Gumbo Parser](https://github.com/google/gumbo-parser)
- [nlohmann/json.hpp](https://github.com/nlohmann/json)
- [cxxopts.hpp](https://github.com/jarro2783/cxxopts)

## Build
```bash
g++ crawly.cpp -o crawly -I./include -lcurl -lgumbo -pthread -std=c++17
```

## Usage

```bash
./crawly --start-url https://example.com --max-pages 50 --threads 4 --verbose
```

## CLI options
```
--start-url : Starting URL (required)

--max-pages : Maximum pages to crawl (default: 50)

--max-depth : Maximum link depth (default: 3)

--threads : Number of threads (default: 4)

--output : Output JSON file (default: crawly_results.json)

--delay : Delay between requests in ms (default: 200)

--verbose : Print detailed crawl info

-v, --version : Show version
```

