#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <thread>

#include "asio/io_context.hpp"
#include "async_simple/coro/SyncAwait.h"
#include "coro_io/coro_io.hpp"
#include "coro_io/coro_file.hpp"
#include "coro_io/io_context_pool.hpp"
#include "doctest.h"

namespace fs = std::filesystem;

constexpr uint64_t KB = 1024;
constexpr uint64_t MB = 1024 * KB;
constexpr uint64_t block_size = 4 * KB;

std::vector<char> create_filled_vec(std::string fill_with,
                                    size_t size = block_size) {
  if (fill_with.empty() || size == 0)
    return std::vector<char>{};
  std::vector<char> ret(size);
  size_t fill_with_size = fill_with.size();
  int cnt = size / fill_with_size;
  int remain = size % fill_with_size;
  for (int i = 0; i < cnt; i++) {
    memcpy(ret.data() + i * fill_with_size, fill_with.data(), fill_with_size);
  }
  if (remain > 0) {
    memcpy(ret.data() + size - remain, fill_with.data(), remain);
  }
  return ret;
}
void create_file(std::string filename, size_t file_size,
                 const std::vector<char>& fill_with_vec) {
  std::ofstream file(filename, std::ios::binary);
  file.exceptions(std::ios_base::failbit | std::ios_base::badbit);

  if (!file) {
    std::cout << "create file failed\n";
    return;
  }
  size_t fill_with_size = fill_with_vec.size();
  if (file_size == 0 || fill_with_size == 0) {
    return;
  }
  int cnt = file_size / block_size;
  int remain = file_size - block_size * cnt;
  for (size_t i = 0; i < cnt; i++) {
    file.write(fill_with_vec.data(), block_size);
  }
  if (remain > 0) {
    file.write(fill_with_vec.data(), remain);
  }
  file.flush();  // can throw
  return;
}

TEST_CASE("small_file_read_test") {
  std::string filename = "small_file_read_test.txt";
  std::string fill_with = "small_file_read_test";
  auto block_vec = create_filled_vec(fill_with);
  create_file(filename, 1 * KB, block_vec);
  asio::io_context ioc;
  auto work = std::make_unique<asio::io_context::work>(ioc);
  std::thread thd([&ioc] {
    ioc.run();
  });

  ylt::coro_file file(ioc.get_executor(), filename);
  CHECK(file.is_open());

  char buf[block_size]{};
  while (!file.eof()) {
    auto [ec, read_size] =
        async_simple::coro::syncAwait(file.async_read(buf, block_size));
    if (ec) {
      std::cout << ec.message() << "\n";
      break;
    }
    std::cout << read_size << std::endl;
    CHECK(std::string_view(block_vec.data(), read_size) ==
          std::string_view(buf, read_size));
  }
  work.reset();
  thd.join();
  fs::remove(fs::path(filename));
}
TEST_CASE("large_file_read_test") {
  std::string filename = "large_file_read_test.txt";
  std::string fill_with = "large_file_read_test";
  size_t file_size = 100 * MB;
  auto block_vec = create_filled_vec(fill_with);
  create_file(filename, file_size, block_vec);
  CHECK(fs::file_size(filename) == file_size);
  asio::io_context ioc;
  auto work = std::make_unique<asio::io_context::work>(ioc);
  std::thread thd([&ioc] {
    ioc.run();
  });

  ylt::coro_file file(ioc.get_executor(), filename);
  CHECK(file.is_open());

  char buf[block_size]{};
  size_t total_size = 0;
  while (!file.eof()) {
    auto [ec, read_size] =
        async_simple::coro::syncAwait(file.async_read(buf, block_size));
    if (ec) {
      std::cout << ec.message() << "\n";
      break;
    }
    total_size += read_size;
    CHECK(std::string_view(block_vec.data(), read_size) ==
          std::string_view(buf, read_size));
  }
  CHECK(total_size == file_size);
  work.reset();
  thd.join();
  fs::remove(fs::path(filename));
}
TEST_CASE("empty_file_read_test") {
  std::string filename = "empty_file_read_test.txt";
  std::string fill_with = "";
  auto block_vec = create_filled_vec(fill_with);
  create_file(filename, 0, block_vec);
  asio::io_context ioc;
  auto work = std::make_unique<asio::io_context::work>(ioc);
  std::thread thd([&ioc] {
    ioc.run();
  });

  ylt::coro_file file(ioc.get_executor(), filename);
  CHECK(file.is_open());

  char buf[block_size]{};
  auto [ec, read_size] =
      async_simple::coro::syncAwait(file.async_read(buf, block_size));
  if (ec) {
    std::cout << ec.message() << "\n";
  }
  auto read_content = std::string_view(buf, read_size);
  CHECK(read_size == 0);
  CHECK(read_content.empty());
  work.reset();
  thd.join();
  fs::remove(fs::path(filename));
}
TEST_CASE("small_file_read_with_pool_test") {
  std::string filename = "small_file_read_with_pool_test.txt";
  std::string fill_with = "small_file_read_with_pool_test";
  size_t file_size = 1 * KB;
  auto block_vec = create_filled_vec(fill_with);
  create_file(filename, file_size, block_vec);
  CHECK(fs::file_size(filename) == file_size);
  coro_io::io_context_pool pool(std::thread::hardware_concurrency());
  std::thread thd([&pool] {
    pool.run();
  });

  ylt::coro_file file(*pool.get_executor(), filename);
  CHECK(file.is_open());

  char buf[block_size]{};
  while (!file.eof()) {
    auto [ec, read_size] =
        async_simple::coro::syncAwait(file.async_read(buf, block_size));
    if (ec) {
      std::cout << ec.message() << "\n";
      break;
    }
    std::cout << read_size << std::endl;
    CHECK(std::string_view(block_vec.data(), read_size) ==
          std::string_view(buf, read_size));
  }
  pool.stop();
  thd.join();
  fs::remove(fs::path(filename));
}
TEST_CASE("large_file_read_with_pool_test") {
  std::string filename = "large_file_read_with_pool_test.txt";
  std::string fill_with = "large_file_read_with_pool_test";
  size_t file_size = 100 * MB;
  auto block_vec = create_filled_vec(fill_with);
  create_file(filename, file_size, block_vec);
  CHECK(fs::file_size(filename) == file_size);
  coro_io::io_context_pool pool(std::thread::hardware_concurrency());
  std::thread thd([&pool] {
    pool.run();
  });

  ylt::coro_file file(*pool.get_executor(), filename);
  CHECK(file.is_open());

  char buf[block_size]{};
  size_t total_size = 0;
  while (!file.eof()) {
    auto [ec, read_size] =
        async_simple::coro::syncAwait(file.async_read(buf, block_size));
    if (ec) {
      std::cout << ec.message() << "\n";
      break;
    }
    total_size += read_size;
    CHECK(std::string_view(block_vec.data(), read_size) ==
          std::string_view(buf, read_size));
  }
  CHECK(total_size == file_size);
  pool.stop();
  thd.join();
  fs::remove(fs::path(filename));
}
TEST_CASE("small_file_write_test") {
  std::string filename = "small_file_write_test.txt";
  asio::io_context ioc;
  auto work = std::make_unique<asio::io_context::work>(ioc);
  std::thread thd([&ioc] {
    ioc.run();
  });

  ylt::coro_file file(ioc.get_executor(), filename);
  CHECK(file.is_open());

  char buf[512]{};

  std::string file_content_0 = "small_file_write_test_0";

  auto ec = async_simple::coro::syncAwait(
      file.async_write(file_content_0.data(), file_content_0.size()));
  if (ec) {
    std::cout << ec.message() << "\n";
  }

  std::ifstream is(filename, std::ios::binary);
  if (!is.is_open()) {
    std::cout << "Failed to open file: " << filename << "\n";
    return;
  }
  is.seekg(0, std::ios::end);
  auto size = is.tellg();
  is.seekg(0, std::ios::beg);
  is.read(buf, size);
  CHECK(size == file_content_0.size());
  is.close();
  auto read_content = std::string_view(buf, size);
  std::cout << read_content << "\n";
  CHECK(read_content == file_content_0);

  std::string file_content_1 = "small_file_write_test_1";

  ec = async_simple::coro::syncAwait(
      file.async_write(file_content_1.data(), file_content_1.size()));
  if (ec) {
    std::cout << ec.message() << "\n";
  }
  is.open(filename, std::ios::binary);
  if (!is.is_open()) {
    std::cout << "Failed to open file: " << filename << "\n";
    return;
  }
  is.seekg(0, std::ios::end);
  size = is.tellg();
  is.seekg(0, std::ios::beg);
  CHECK(size == (file_content_0.size() + file_content_1.size()));
  is.read(buf, size);
  is.close();
  read_content = std::string_view(buf, size);
  std::cout << read_content << "\n";
  CHECK(read_content == (file_content_0 + file_content_1));

  work.reset();
  thd.join();
  fs::remove(fs::path(filename));
}
TEST_CASE("large_file_write_test") {
  std::string filename = "large_file_write_test.txt";
  size_t file_size = 100 * MB;
  asio::io_context ioc;
  auto work = std::make_unique<asio::io_context::work>(ioc);
  std::thread thd([&ioc] {
    ioc.run();
  });

  ylt::coro_file file(ioc.get_executor(), filename);
  CHECK(file.is_open());

  auto block_vec = create_filled_vec("large_file_write_test");
  int cnt = file_size / block_size;
  int remain = file_size % block_size;
  while (cnt--) {
    auto ec = async_simple::coro::syncAwait(
        file.async_write(block_vec.data(), block_size));
    if (ec) {
      std::cout << ec.message() << "\n";
      break;
    }
  }
  if (remain > 0) {
    auto ec = async_simple::coro::syncAwait(
        file.async_write(block_vec.data(), remain));
    if (ec) {
      std::cout << ec.message() << "\n";
    }
  }
  CHECK(fs::file_size(filename) == file_size);
  std::ifstream is(filename, std::ios::binary);
  if (!is.is_open()) {
    std::cout << "Failed to open file: " << filename << "\n";
    return;
  }
  is.seekg(0, std::ios::end);
  auto size = is.tellg();
  is.seekg(0, std::ios::beg);
  CHECK(size == file_size);

  std::vector<char> read_content(block_size);
  while (!is.eof()) {
    is.read(read_content.data(), block_size);
    CHECK(std::string_view(read_content.data(), is.gcount()) ==
          std::string_view(block_vec.data(), is.gcount()));
  }
  is.close();
  work.reset();
  thd.join();
  fs::remove(fs::path(filename));
}
TEST_CASE("empty_file_write_test") {
  std::string filename = "empty_file_write_test.txt";
  asio::io_context ioc;
  auto work = std::make_unique<asio::io_context::work>(ioc);
  std::thread thd([&ioc] {
    ioc.run();
  });

  ylt::coro_file file(ioc.get_executor(), filename);
  CHECK(file.is_open());

  char buf[512]{};

  std::string file_content_0 = "small_file_write_test_0";

  auto ec =
      async_simple::coro::syncAwait(file.async_write(file_content_0.data(), 0));
  if (ec) {
    std::cout << ec.message() << "\n";
  }

  std::ifstream is(filename, std::ios::binary);
  if (!is.is_open()) {
    std::cout << "Failed to open file: " << filename << "\n";
    return;
  }
  is.seekg(0, std::ios::end);
  auto size = is.tellg();
  CHECK(size == 0);
  is.close();
  work.reset();
  thd.join();
  fs::remove(fs::path(filename));
}
TEST_CASE("small_file_write_with_pool_test") {
  std::string filename = "small_file_write_with_pool_test.txt";
  coro_io::io_context_pool pool(std::thread::hardware_concurrency());
  std::thread thd([&pool] {
    pool.run();
  });

  ylt::coro_file file(*pool.get_executor(), filename);
  CHECK(file.is_open());

  char buf[512]{};

  std::string file_content_0 = "small_file_write_with_pool_test_0";

  auto ec = async_simple::coro::syncAwait(
      file.async_write(file_content_0.data(), file_content_0.size()));
  if (ec) {
    std::cout << ec.message() << "\n";
  }

  std::ifstream is(filename, std::ios::binary);
  if (!is.is_open()) {
    std::cout << "Failed to open file: " << filename << "\n";
    return;
  }
  is.seekg(0, std::ios::end);
  auto size = is.tellg();
  is.seekg(0, std::ios::beg);
  is.read(buf, size);
  CHECK(size == file_content_0.size());
  is.close();
  auto read_content = std::string_view(buf, size);
  std::cout << read_content << "\n";
  CHECK(read_content == file_content_0);

  std::string file_content_1 = "small_file_write_with_pool_test_1";

  ec = async_simple::coro::syncAwait(
      file.async_write(file_content_1.data(), file_content_1.size()));
  if (ec) {
    std::cout << ec.message() << "\n";
  }
  is.open(filename, std::ios::binary);
  if (!is.is_open()) {
    std::cout << "Failed to open file: " << filename << "\n";
    return;
  }
  is.seekg(0, std::ios::end);
  size = is.tellg();
  is.seekg(0, std::ios::beg);
  CHECK(size == (file_content_0.size() + file_content_1.size()));
  is.read(buf, size);
  is.close();
  read_content = std::string_view(buf, size);
  std::cout << read_content << "\n";
  CHECK(read_content == (file_content_0 + file_content_1));

  pool.stop();
  thd.join();
  fs::remove(fs::path(filename));
}
TEST_CASE("large_file_write_with_pool_test") {
  std::string filename = "large_file_write_with_pool_test.txt";
  size_t file_size = 100 * MB;
  coro_io::io_context_pool pool(std::thread::hardware_concurrency());
  std::thread thd([&pool] {
    pool.run();
  });

  ylt::coro_file file(*pool.get_executor(), filename);
  CHECK(file.is_open());

  auto block_vec = create_filled_vec("large_file_write_with_pool_test");
  int cnt = file_size / block_size;
  int remain = file_size % block_size;
  while (cnt--) {
    auto ec = async_simple::coro::syncAwait(
        file.async_write(block_vec.data(), block_size));
    if (ec) {
      std::cout << ec.message() << "\n";
      break;
    }
  }
  if (remain > 0) {
    auto ec = async_simple::coro::syncAwait(
        file.async_write(block_vec.data(), remain));
    if (ec) {
      std::cout << ec.message() << "\n";
    }
  }
  CHECK(fs::file_size(filename) == file_size);
  std::ifstream is(filename, std::ios::binary);
  if (!is.is_open()) {
    std::cout << "Failed to open file: " << filename << "\n";
    return;
  }
  is.seekg(0, std::ios::end);
  auto size = is.tellg();
  is.seekg(0, std::ios::beg);
  CHECK(size == file_size);

  std::vector<char> read_content(block_size);
  while (!is.eof()) {
    is.read(read_content.data(), block_size);
    CHECK(std::string_view(read_content.data(), is.gcount()) ==
          std::string_view(block_vec.data(), is.gcount()));
  }
  is.close();
  pool.stop();
  thd.join();
  fs::remove(fs::path(filename));
}
