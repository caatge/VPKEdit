#include <vpkedit/PackFile.h>

#include <cstring>
#include <filesystem>
#include <utility>

#include <vpkedit/detail/Misc.h>
#include <vpkedit/BSP.h>
#include <vpkedit/GCF.h>
#include <vpkedit/GMA.h>
#include <vpkedit/VPK.h>
#include <vpkedit/ZIP.h>

using namespace vpkedit;
using namespace vpkedit::detail;

PackFile::PackFile(std::string fullFilePath_, PackFileOptions options_)
		: fullFilePath(std::move(fullFilePath_))
		, options(options_) {}

std::unique_ptr<PackFile> PackFile::open(const std::string& path, PackFileOptions options, const Callback& callback) {
	auto extension = std::filesystem::path(path).extension().string();
	if (PackFile::getExtensionRegistry().contains(extension)) {
		return PackFile::getExtensionRegistry()[extension](path, options, callback);
	}
	return nullptr;
}

PackFileType PackFile::getType() const {
	return this->type;
}

PackFileOptions PackFile::getOptions() const {
	return this->options;
}

std::vector<std::string> PackFile::verifyEntryChecksums() const {
	return {};
}

bool PackFile::verifyFileChecksum() const {
	return true;
}

std::optional<Entry> PackFile::findEntry(const std::string& filename_, bool includeUnbaked) const {
	auto filename = filename_;
	::normalizeSlashes(filename);
	if (!this->options.allowUppercaseLettersInFilenames) {
		::toLowerCase(filename);
	}
	auto [dir, name] = ::splitFilenameAndParentDir(filename);

	if (this->entries.contains(dir)) {
		for (const Entry& entry : this->entries.at(dir)) {
			if (entry.path == filename) {
				return entry;
			}
		}
	}
	if (includeUnbaked && this->unbakedEntries.contains(dir)) {
		for (const Entry& unbakedEntry : this->unbakedEntries.at(dir)) {
			if (unbakedEntry.path == filename) {
				return unbakedEntry;
			}
		}
	}
	return std::nullopt;
}

std::optional<std::string> PackFile::readEntryText(const Entry& entry) const {
	auto bytes = this->readEntry(entry);
	if (!bytes) {
		return std::nullopt;
	}
	std::string out;
	for (auto byte : *bytes) {
		if (byte == static_cast<std::byte>(0))
			break;
		out += static_cast<char>(byte);
	}
	return out;
}

bool PackFile::isReadOnly() const {
	return false;
}

void PackFile::addEntry(const std::string& filename_, const std::string& pathToFile, EntryOptions options_) {
	if (this->isReadOnly()) {
		return;
	}

	auto buffer = ::readFileData(pathToFile, 0);

	Entry entry{};
	entry.unbaked = true;
	entry.unbakedUsingByteBuffer = false;
	entry.unbakedData = pathToFile;

	Entry& finalEntry = this->addEntryInternal(entry, filename_, buffer, options_);
	finalEntry.unbakedData = pathToFile;
}

void PackFile::addEntry(const std::string& filename_, std::vector<std::byte>&& buffer, EntryOptions options_) {
	if (this->isReadOnly()) {
		return;
	}

	Entry entry{};
	entry.unbaked = true;
	entry.unbakedUsingByteBuffer = true;

	Entry& finalEntry = this->addEntryInternal(entry, filename_, buffer, options_);
	finalEntry.unbakedData = std::move(buffer);
}

void PackFile::addEntry(const std::string& filename_, const std::byte* buffer, std::uint64_t bufferLen, EntryOptions options_) {
	std::vector<std::byte> data;
	data.resize(bufferLen);
	std::memcpy(data.data(), buffer, bufferLen);
	this->addEntry(filename_, std::move(data), options_);
}

bool PackFile::removeEntry(const std::string& filename_) {
	if (this->isReadOnly()) {
		return false;
	}

	auto filename = filename_;
	if (!this->options.allowUppercaseLettersInFilenames) {
		::toLowerCase(filename);
	}
	auto [dir, name] = ::splitFilenameAndParentDir(filename);

	// Check unbaked entries first
	if (this->unbakedEntries.contains(dir)) {
		for (auto& [preexistingDir, unbakedEntryVec] : this->unbakedEntries) {
			if (preexistingDir != dir) {
				continue;
			}
			for (auto it = unbakedEntryVec.begin(); it != unbakedEntryVec.end(); ++it) {
				if (it->path == filename) {
					unbakedEntryVec.erase(it);
					return true;
				}
			}
		}
	}

	// If it's not in regular entries either you can't remove it!
	if (!this->entries.contains(dir))
		return false;

	for (auto it = this->entries.at(dir).begin(); it != this->entries.at(dir).end(); ++it) {
		if (it->path == filename) {
			this->entries.at(dir).erase(it);
			return true;
		}
	}
	return false;
}

const std::unordered_map<std::string, std::vector<Entry>>& PackFile::getBakedEntries() const {
	return this->entries;
}

const std::unordered_map<std::string, std::vector<Entry>>& PackFile::getUnbakedEntries() const {
	return this->unbakedEntries;
}

std::size_t PackFile::getEntryCount(bool includeUnbaked) const {
	std::size_t count = 0;
	for (const auto& [directory, entries_] : this->entries) {
		count += entries_.size();
	}
	if (includeUnbaked) {
		for (const auto& [directory, entries_] : this->unbakedEntries) {
			count += entries_.size();
		}
	}
	return count;
}

std::string_view PackFile::getFilepath() const {
	return this->fullFilePath;
}

std::string PackFile::getTruncatedFilepath() const {
	return (std::filesystem::path{this->fullFilePath}.parent_path() / this->getTruncatedFilestem()).string();
}

std::string PackFile::getFilename() const {
	return std::filesystem::path{this->fullFilePath}.filename().string();
}

std::string PackFile::getTruncatedFilename() const {
	const std::filesystem::path path{this->fullFilePath};
	return (path.parent_path() / this->getTruncatedFilestem()).string() + path.extension().string();
}

std::string PackFile::getFilestem() const {
	return std::filesystem::path{this->fullFilePath}.stem().string();
}

std::string PackFile::getTruncatedFilestem() const {
	return this->getFilestem();
}

std::vector<std::string> PackFile::getSupportedFileTypes() {
	std::vector<std::string> out;
	for (const auto& [extension, factoryFunction] : PackFile::getExtensionRegistry()) {
		out.push_back(extension);
	}
	return out;
}

std::string PackFile::getBakeOutputDir(const std::string& outputDir) const {
	std::string out = outputDir;
	if (!out.empty()) {
		::normalizeSlashes(out);
	} else {
		out = this->fullFilePath;
		auto lastSlash = out.rfind('/');
		if (lastSlash != std::string::npos) {
			out = this->getFilepath().substr(0, lastSlash);
		} else {
			out = ".";
		}
	}
	return out;
}

void PackFile::mergeUnbakedEntries() {
	for (auto& [dir, unbakedEntriesAndData] : this->unbakedEntries) {
		for (Entry& unbakedEntry : unbakedEntriesAndData) {
			if (!this->entries.contains(dir)) {
				this->entries[dir] = {};
			}

			unbakedEntry.unbaked = false;

			// Clear any data that might be stored in it
			unbakedEntry.unbakedUsingByteBuffer = false;
			unbakedEntry.unbakedData = "";

			this->entries.at(dir).push_back(unbakedEntry);
		}
	}
	this->unbakedEntries.clear();
}

void PackFile::setFullFilePath(const std::string& outputDir) {
	// Assumes PackFile::getBakeOutputDir is the input for outputDir
	this->fullFilePath = outputDir + '/' + this->getFilename();
}

Entry PackFile::createNewEntry() {
	return {};
}

const std::variant<std::string, std::vector<std::byte>>& PackFile::getEntryUnbakedData(const Entry& entry) {
	return entry.unbakedData;
}

bool PackFile::isEntryUnbakedUsingByteBuffer(const Entry& entry) {
	return entry.unbakedUsingByteBuffer;
}

std::unordered_map<std::string, PackFile::FactoryFunction>& PackFile::getExtensionRegistry() {
	static std::unordered_map<std::string, PackFile::FactoryFunction> extensionRegistry;
	return extensionRegistry;
}

const PackFile::FactoryFunction& PackFile::registerExtensionForTypeFactory(std::string_view extension, const FactoryFunction& factory) {
	PackFile::getExtensionRegistry()[std::string{extension}] = factory;
	return factory;
}

PackFileReadOnly::PackFileReadOnly(std::string fullFilePath_, PackFileOptions options_)
		: PackFile(std::move(fullFilePath_), options_) {}

bool PackFileReadOnly::isReadOnly() const {
	return true;
}

Entry& PackFileReadOnly::addEntryInternal(Entry& entry, const std::string& filename_, std::vector<std::byte>& buffer, EntryOptions options_) {
	return entry; // Stubbed
}

bool PackFileReadOnly::bake(const std::string& outputDir_ /*= ""*/, const Callback& callback /*= nullptr*/) {
	return false; // Stubbed
}
