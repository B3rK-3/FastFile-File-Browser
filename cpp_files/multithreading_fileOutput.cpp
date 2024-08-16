#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <thread>
#include <vector>

#include "../libraries/rapidjson/document.h"
#include "../libraries/rapidjson/filereadstream.h"
#include "../libraries/rapidjson/stringbuffer.h"
#include "../libraries/rapidjson/writer.h"

using namespace std;
namespace fs = std::filesystem;
namespace rj = rapidjson;

void helper(const vector<fs::path> &dirs);
void indexer(const fs::directory_entry &ent, rj::Document *extensionData, rj::Document *filenameData, rj::Document::AllocatorType &extensionDataAllocator, rj::Document::AllocatorType &filenameDataAllocator);
void writeBuffer();

std::mutex data_mutex;
std::mutex count_mutex;
std::mutex indexer_mutex;
const long MAX_COUNT = 50000;
const int MAX_THREADS = 16;
const int MaxSubFolderInMemory = 50000;
int COUNT = 0;

vector<fs::directory_entry> filesNFolders;

int main() {
    try {
        fs::path root_path = R"(C:\)";
        vector<fs::path> initial_dirs;

        // Get the initial set of directories at the root level
        for (const auto &entry : fs::directory_iterator(root_path)) {
            if (fs::is_directory(entry.status())) {
                initial_dirs.push_back(entry.path());
            }
        }

        // Split directories among threads
        vector<vector<fs::path>> thread_dirs(initial_dirs.size());
        for (size_t i = 0; i < initial_dirs.size(); ++i) {
            thread_dirs[i % MAX_THREADS].push_back(initial_dirs[i]);
        }

        vector<thread> threads;
        chrono::steady_clock::time_point begin = chrono::steady_clock::now();

        // Launch threads
        for (int i = 0; i < initial_dirs.size(); ++i) {
            threads.emplace_back(helper, ref(thread_dirs[i]));
        }

        // Wait for threads to finish
        for (auto &t : threads) {
            t.join();
        }
        writeBuffer();
        // For testing purposes
        chrono::steady_clock::time_point end = chrono::steady_clock::now();
        cout << "File Count: " << COUNT << endl;
        cout << "Elapsed time: "
             << chrono::duration_cast<chrono::microseconds>(end - begin).count() /
                    1000000.0
             << " seconds" << endl;
    } catch (const fs::filesystem_error &e) {
        cerr << "Filesystem error: " << e.what() << endl;
    } catch (const std::exception &e) {
        cerr << "General error: " << e.what() << endl;
    }
    return 0;
}

void helper(const vector<fs::path> &dirs) {
    int fileNFolderCount = 0;
    for (const auto &dir : dirs) {
        vector<fs::path> stack;
        stack.push_back(dir);

        while (!stack.empty()) {
            fs::path current_dir = stack.back();
            stack.pop_back();

            try {
                for (const auto &entry : fs::directory_iterator(current_dir)) {
                    std::lock_guard<std::mutex> guard(data_mutex);
                    filesNFolders.push_back(entry);
                    fileNFolderCount++;
                    if (fileNFolderCount >= MaxSubFolderInMemory) {
                        fileNFolderCount = 0;
                        writeBuffer();
                    }
                    if (COUNT >= MAX_COUNT) {
                        return;
                    }

                    if (fs::is_directory(entry.status())) {
                        COUNT++;
                        stack.push_back(entry.path());
                    }
                }
            } catch (const fs::filesystem_error &e) {
                continue;
            }
        }
    }
}

void indexer(const fs::directory_entry &ent, rj::Document *extensionData, rj::Document *filenameData, rj::Document::AllocatorType &extensionDataAllocator, rj::Document::AllocatorType &filenameDataAllocator) {
    std::lock_guard<std::mutex> guard(indexer_mutex);
    const string &path = ent.path().string();
    string fileName = path.substr(path.find_last_of('\\') + 1);
    rj::Value *loc_data;
    int ix = fileName.find_last_of('.');

    if (!fs::is_directory(ent.status()) && ix != string::npos) {
        loc_data = extensionData;

        for (int i = ix + 1; i < fileName.size(); i++) {
            string chr(1, fileName[i]);
            if (!isalnum(fileName[i])) {
                continue;
            }
            if (!loc_data->HasMember(chr.c_str())) {
                loc_data->AddMember(rj::Value(chr.c_str(), extensionDataAllocator), rj::Value(rj::kObjectType), extensionDataAllocator);
            }
            loc_data = &(*loc_data)[chr.c_str()];  // Update loc_data to point to the nested object
            if (i == fileName.size() - 1) {
                if (!loc_data->HasMember("END")) {
                    rj::Value endArray(rj::kArrayType);
                    loc_data->AddMember("END", endArray, extensionDataAllocator);
                }
                loc_data->FindMember("END")->value.PushBack(rj::Value().SetString(path.c_str(), extensionDataAllocator), extensionDataAllocator);  // Modify DATA directly
            }
        }
    }
    fileName = fileName.substr(0, ix);
    loc_data = filenameData;  // Use a pointer to json to modify DATA directly

    for (int i = 0; i < fileName.size(); i++) {
        if (!isalnum(fileName[i])) {
            continue;
        }
        string chr(1, tolower(fileName[i]));
        if (!loc_data->HasMember(chr.c_str())) {
            loc_data->AddMember(rj::Value(chr.c_str(), filenameDataAllocator), rj::Value(rj::kObjectType), filenameDataAllocator);
        }
        loc_data = &((*loc_data)[chr.c_str()]);  // Update loc_data to point to the nested object
        if (i == fileName.size() - 1) {
            if (!loc_data->HasMember("END")) {
                rj::Value endArray(rj::kArrayType);
                loc_data->AddMember("END", endArray, filenameDataAllocator);
            }
            loc_data->FindMember("END")->value.PushBack(rj::Value().SetString(path.c_str(), filenameDataAllocator), filenameDataAllocator);  // Modify DATA directly
        }
    }
}
void writeBuffer() {
    // Debug print to trace execution
    cout << "Entering writeBuffer" << endl;

    // Lock the mutex (if necessary, ensure no deadlock)
    if (count_mutex.try_lock()) {
        cout << "Mutex locked successfully" << endl;
    } else {
        cerr << "Failed to lock mutex" << endl;
        return;
    }

    // Open files in read mode
    ifstream filenameJson("../fileIndex.json", ios::in | ios::binary);
    ifstream extensionJson("../extIndex.json", ios::in | ios::binary);

    if (!filenameJson.is_open() || !extensionJson.is_open()) {
        std::cerr << "Error opening files for reading!" << std::endl;
        count_mutex.unlock();
        return;
    }

    rj::Document extensionData;
    rj::Document filenameData;

    rj::Document::AllocatorType &filenameDataAllocator = filenameData.GetAllocator();
    rj::Document::AllocatorType &extensionDataAllocator = extensionData.GetAllocator();

    // Read and parse existing extensionJson
    string extJsonContent((istreambuf_iterator<char>(extensionJson)), istreambuf_iterator<char>());
    if (!extJsonContent.empty()) {
        rj::ParseResult extParseResult = extensionData.Parse(extJsonContent.c_str());
        if (!extParseResult) {
            std::cerr << "Error parsing extensionJson: " << rj::GetParseErrorFunc(extParseResult.Code())
                      << " at offset " << extParseResult.Offset() << std::endl;
            count_mutex.unlock();
            return;
        }
    } else {
        extensionData.SetObject();
    }

    // Read and parse existing filenameJson
    string fileJsonContent((istreambuf_iterator<char>(filenameJson)), istreambuf_iterator<char>());
    if (!fileJsonContent.empty()) {
        rj::ParseResult fileParseResult = filenameData.Parse(fileJsonContent.c_str());
        if (!fileParseResult) {
            std::cerr << "Error parsing filenameJson: " << rj::GetParseErrorFunc(fileParseResult.Code())
                      << " at offset " << fileParseResult.Offset() << std::endl;
            count_mutex.unlock();
            return;
        }
    } else {
        filenameData.SetObject();
    }

    filenameJson.close();
    extensionJson.close();

    // Process the files and folders and merge into existing data
    for (fs::directory_entry &each : filesNFolders) {
        indexer(each, &extensionData, &filenameData, extensionDataAllocator, filenameDataAllocator);
    }

    // Open files in write mode (clear the content)
    ofstream outFileFilename("../fileIndex.json", ios::out | ios::trunc | ios::binary);
    ofstream outFileExtension("../extIndex.json", ios::out | ios::trunc | ios::binary);

    if (!outFileFilename.is_open() || !outFileExtension.is_open()) {
        std::cerr << "Error opening files for writing!" << std::endl;
        count_mutex.unlock();
        return;
    }

    // Write the updated JSON data back to the files
    {
        rj::StringBuffer filenameBuffer;
        rj::Writer<rj::StringBuffer> writer1(filenameBuffer);
        filenameData.Accept(writer1);
        outFileFilename.write(filenameBuffer.GetString(), filenameBuffer.GetSize());
    }

    {
        rj::StringBuffer extensionBuffer;
        rj::Writer<rj::StringBuffer> writer2(extensionBuffer);
        extensionData.Accept(writer2);
        outFileExtension.write(extensionBuffer.GetString(), extensionBuffer.GetSize());
    }

    // Close the files
    outFileFilename.close();
    outFileExtension.close();

    // Unlock the mutex
    count_mutex.unlock();

    // Clear the list of files and folders after processing
    filesNFolders.clear();

    // Debug print to confirm execution reaches the end
    cout << "Exiting writeBuffer" << endl;
}