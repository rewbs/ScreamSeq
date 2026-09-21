#include "editor/TrackerDocument.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <windows.h>

static void require(bool value, const char *message) {
	if(!value) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
	try {
		if(argc != 2) throw std::runtime_error("Pass a disposable scratch directory");
		auto folder = std::filesystem::u8path(argv[1]);
		std::filesystem::create_directories(folder);
		auto path = folder / L"atomic-\u97f3\u697d.mptm";
		auto encoded = path.u8string();
		std::string utf8(encoded.begin(), encoded.end());
		auto doc = Tracker::Document::demo();
		doc->save(utf8);
		require(std::filesystem::is_regular_file(path), "Unicode destination was not created");
		auto firstSize = std::filesystem::file_size(path);
		doc->save(utf8);
		require(std::filesystem::file_size(path) == firstSize, "Replacement save changed serialized size");
		std::ifstream saved(path, std::ios::binary);
		std::vector<char> chars((std::istreambuf_iterator<char>(saved)), {});
		std::vector<std::byte> bytes(chars.size());
		std::memcpy(bytes.data(), chars.data(), chars.size());
		Tracker::Document reopened(bytes);
		require(reopened.cell(0, 0, 0) == doc->cell(0, 0, 0), "Saved module did not reopen");
		auto reopenedPath = Tracker::Document::open(utf8);
		require(reopenedPath->cell(0, 0, 0) == doc->cell(0, 0, 0), "Unicode public open did not round-trip");
		auto occupied = folder / "occupied";
		std::filesystem::create_directories(occupied);
		std::ofstream(occupied / "keep.txt") << "original";
		bool rejected = false;
		try { doc->save(occupied.string()); } catch(const std::exception &) { rejected = true; }
		require(rejected && std::filesystem::is_regular_file(occupied / "keep.txt"), "Failed replacement did not preserve destination");
		for(const auto &file : std::filesystem::directory_iterator(folder))
			require(file.path().filename().wstring().find(L".writing.") == std::wstring::npos, "Failed save leaked temporary file");
		std::cout << "Unicode save, existing-file replacement, reopen, failure preservation and temporary cleanup passed\n";
		return 0;
	} catch(const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
