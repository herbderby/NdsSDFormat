#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <print>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

using std::println;

// Helper to run shell commands and capture output
std::pair<int, std::string> runCommand(const std::string& cmd) {
  std::array<char, 128> buffer;
  std::string result;
  std::string fullCmd = cmd + " 2>&1";  // Capture stderr too
  FILE* pipe = popen(fullCmd.c_str(), "r");

  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    result += buffer.data();
  }

  int status = pclose(pipe);
  int rc = -1;
  if (WIFEXITED(status)) {
    rc = WEXITSTATUS(status);
  }

  return {rc, result};
}

// Parse size string to bytes (disk arithmetic: 1MB = 10^6, 1GB = 10^9)
uint64_t parseSize(const std::string& sizeStr) {
  uint64_t multiplier = 1;
  std::string numPart = sizeStr;

  if (sizeStr.find("MB") != std::string::npos) {
    multiplier = 1000 * 1000;
    numPart = sizeStr.substr(0, sizeStr.find("MB"));
  } else if (sizeStr.find("GB") != std::string::npos) {
    multiplier = 1000 * 1000 * 1000;
    numPart = sizeStr.substr(0, sizeStr.find("GB"));
  }

  return std::stoull(numPart) * multiplier;
}

void createImage(const std::string& filename, uint64_t sizeBytes) {
  FILE* f = fopen(filename.c_str(), "wb");
  if (!f) {
    throw std::runtime_error("Failed to create image file");
  }

  // Resize file
  if (ftruncate(fileno(f), sizeBytes) != 0) {
    fclose(f);
    throw std::runtime_error("ftruncate failed");
  }
  fclose(f);
}

void fillRandom(const std::string& filename) {
  // Strategy: Write 16MB of garbage at the start to ensure MBR/FAT tables are
  // dirty.
  FILE* f = fopen(filename.c_str(), "rb+");
  if (!f) {
    return;
  }

  srand(time(nullptr));
  const size_t bufSize = 1000 * 1000;  // 1MB (Decimal)
  std::vector<uint8_t> buffer(bufSize);

  // Pollute first 32MB
  for (int i = 0; i < 32; i++) {
    for (size_t b = 0; b < bufSize; b++) {
      buffer[b] = rand() % 256;
    }
    fwrite(buffer.data(), 1, bufSize, f);
  }

  fclose(f);
}

struct AttachedDevice {
  std::string wholeDisk;
  std::string partition;
};

AttachedDevice attachImage(const std::string& filename) {
  auto [rc, output] = runCommand("hdiutil attach -nomount -plist " + filename);
  println("    [DEBUG] hdiutil output: {}", output);

  AttachedDevice result;

  // Regex to find <key>dev-entry</key> followed by <string>path</string>
  // \s* matches any whitespace including newlines
  std::regex devEntryRegex(
      R"(<key>dev-entry</key>\s*<string>([^<]+)</string>)");

  auto begin =
      std::sregex_iterator(output.begin(), output.end(), devEntryRegex);
  auto end = std::sregex_iterator();

  for (std::sregex_iterator i = begin; i != end; ++i) {
    std::string path = (*i)[1].str();
    // Check if it's a partition (digit + 's' + digit) or whole disk
    if (path.find("s") != std::string::npos &&
        path.find("s", path.find("/dev/disk") + 9) != std::string::npos) {
      if (result.partition.empty()) {
        result.partition = path;
      }
    } else {
      if (result.wholeDisk.empty()) {
        result.wholeDisk = path;
      }
    }
  }

  // Fallback: if we found partition but not whole disk, infer it
  if (result.wholeDisk.empty() && !result.partition.empty()) {
    std::regex fallback("(/dev/disk[0-9]+)");
    std::smatch match;
    if (std::regex_search(result.partition, match, fallback)) {
      result.wholeDisk = match[1].str();
    }
  }

  if (result.wholeDisk.empty() || result.partition.empty()) {
    throw std::runtime_error("Failed to parse hdiutil plist output: " + output);
  }

  return result;
}

void detachImage(const std::string& wholeDisk) {
  std::string cmd = "hdiutil detach " + wholeDisk;
  auto [rc, out] = runCommand(cmd);
  if (rc != 0) {
    println(stderr, "    [!] Failed to detach {}: {}", wholeDisk, out);
    // Try force detach if regular detach fails
    println("    [*] Attempting force detach...");
    runCommand("hdiutil detach -force " + wholeDisk);
  } else {
    println("    [+] Detached {}", wholeDisk);
  }
}

bool runTest(const std::string& sizeStr) {
  println("------------------------------------------------");
  println("Running Test for Size: {}", sizeStr);
  const std::string imgFile = "test.img";
  std::string deviceForCleanup;

  try {
    // 1. Create Image
    println("[*] Creating image...");
    createImage(imgFile, parseSize(sizeStr));

    // 2. Pollute
    println("[*] Polluting with random data...");
    fillRandom(imgFile);

    // 3. Format
    println("[*] Formatting...");
    uint64_t sectorCount = parseSize(sizeStr) / 512;
    std::string cmd = "./build/format_image " + imgFile + " NDS_FAT32 " +
                      std::to_string(sectorCount);
    auto [rc, out] = runCommand(cmd);

    // 4. Attach
    println("[*] Attaching image...");
    const AttachedDevice device = attachImage(imgFile);
    const std::string devicePath = device.partition;
    const std::string wholeDiskPath = device.wholeDisk;

    // Store for cleanup
    deviceForCleanup = wholeDiskPath;

    std::string rawPathCalc = devicePath;
    size_t devPos = rawPathCalc.find("/dev/");
    if (devPos != std::string::npos) {
      rawPathCalc.insert(devPos + 5, "r");
    }
    const std::string rawDevicePath = rawPathCalc;

    println("    Attached as: {} ({}) (Whole: {})", devicePath, rawDevicePath,
            wholeDiskPath);

    bool passed = true;

    // 5. Verify FSCK
    println("[*] Verifying with fsck_msdos...");
    auto [fsckRc, fsckOut] = runCommand("fsck_msdos -n " + rawDevicePath);
    if (fsckRc != 0) {
      println(stderr, "    [!] FSCK Failed (Exit Code {}):\n{}", fsckRc,
              fsckOut);
      passed = false;
    } else {
      println("    [+] FSCK passed.");
    }

    // 6. Verify Mount
    println("[*] Verifying Mount...");
    auto [mountRc, mountOut] = runCommand("diskutil mount " + devicePath);
    println("    [DEBUG] diskutil mount output: {}", mountOut);

    // Poll for mount point
    bool mounted = false;
    for (int i = 0; i < 10; i++) {
      if (fs::exists("/Volumes/NDS_FAT32")) {
        mounted = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (mounted) {
      println("    [+] Volume mounted at /Volumes/NDS_FAT32");
      runCommand("diskutil unmount " + devicePath);
    } else {
      println(stderr, "    [!] Failed to mount!");
      passed = false;
    }

    // 7. Cleanup
    detachImage(wholeDiskPath);
    fs::remove(imgFile);
    deviceForCleanup = "";  // Clear so catch block doesn't retry

    return passed;

  } catch (const std::exception& e) {
    println(stderr, "Exception: {}", e.what());
    if (!deviceForCleanup.empty()) {
      println("[!] Exception caught, attempting to detach {}",
              deviceForCleanup);
      detachImage(deviceForCleanup);
    }
    if (fs::exists(imgFile)) {
      fs::remove(imgFile);
    }
    return false;
  }
}

int main() {
  std::vector<std::string> sizes = {/*"512MB", "1GB", "2GB",*/ "4GB", "8GB",
                                    "16GB", "32GB", "64GB"};

  int failed = 0;

  if (!fs::exists("./build/format_image")) {
    println(stderr, "Error: ./build/format_image not found. Run 'make' first.");
    return 1;
  }

  for (const auto& size : sizes) {
    if (!runTest(size)) {
      println("RESULT: [FAILED] {}", size);
      failed++;
      break;  // Fail fast
    }
    println("RESULT: [PASSED] {}", size);
  }

  println("------------------------------------------------");
  if (failed == 0) {
    println("ALL TESTS PASSED");
    return 0;
  } else {
    println("{} TEST(S) FAILED", failed);
    return 1;
  }
}
