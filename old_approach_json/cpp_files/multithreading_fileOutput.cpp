#include <tchar.h>

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "../libraries/rapidjson/document.h"
#include "../libraries/rapidjson/stringbuffer.h"
#include "../libraries/rapidjson/writer.h"

using namespace std;
namespace fs = filesystem;
namespace rj = rapidjson;

void helper(const vector<fs::path> &dirs);
void indexer(const fs::directory_entry &ent, rj::Document *extensionData, rj::Document *filenameData, rj::Document::AllocatorType &extensionDataAllocator, rj::Document::AllocatorType &filenameDataAllocator);
void writeBuffer();
bool is_not_hidden(const fs::directory_entry &ent);

mutex data_mutex;
mutex count_mutex;
mutex indexer_mutex;

const long MAX_COUNT = 5000;
const int MAX_THREADS = 4;
const int MaxSubFolderInMemory = 100000;
int COUNT = 0;

deque<fs::directory_entry> filesNFolders;
vector<string> ignoredDirectories = {R"(C:\Windows)", R"(C:\ProgramData)", R"(C:\DRIVER)", R"(C:\drivers)", R"(C:\$SysReset)", R"(C:\PerfLogs)", R"(C:\msys64)", R"(C:\vcpkg)"};

int main() {
    try {
        fs::path root_path = R"(C:\)";
        vector<fs::path> initial_dirs;

        for (const auto &entry : fs::directory_iterator(root_path)) {
            bool in = false;
            for (auto &each : ignoredDirectories) {
                if (strcmp(each.c_str(), entry.path().string().c_str()) == 0) {
                    in = true;
                    break;
                }
            }
            filesNFolders.push_back(entry);
            if (fs::is_directory(entry.status()) && !in) {
                initial_dirs.push_back(entry.path());
            }
        }

        vector<vector<fs::path>> thread_dirs(MAX_THREADS);
        for (size_t i = 0; i < initial_dirs.size(); ++i) {
            thread_dirs[i % MAX_THREADS].push_back(initial_dirs[i]);
        }

        vector<thread> threads;
        chrono::steady_clock::time_point begin = chrono::steady_clock::now();

        for (int i = 0; i < MAX_THREADS; ++i) {
            if (!thread_dirs[i].empty()) {
                threads.emplace_back(helper, ref(thread_dirs[i]));
            }
        }

        for (auto &t : threads) {
            t.join();
        }

        if (!filesNFolders.empty()) {
            writeBuffer();
        }

        chrono::steady_clock::time_point end = chrono::steady_clock::now();
        cout << "File Count: " << COUNT << endl;
        cout << "Elapsed time: "
             << chrono::duration_cast<chrono::microseconds>(end - begin).count() /
                    1000000.0
             << " seconds" << endl;
    } catch (const fs::filesystem_error &e) {
        cerr << "fs error: " << e.what() << endl;
    } catch (const exception &e) {
        cerr << "General error: " << e.what() << endl;
    }
    return 0;
}

void helper(const vector<fs::path> &dirs) {
    for (const auto &dir : dirs) {
        deque<fs::path> stack;
        stack.push_back(dir);

        while (!stack.empty()) {
            fs::path current_dir = stack.front();
            stack.pop_front();

            try {
                unique_lock<mutex> guard(data_mutex);
                for (const auto &entry : fs::directory_iterator(current_dir)) {
                    bool in = false;
                    for (auto &each : ignoredDirectories) {
                        if (strcmp(each.c_str(), entry.path().string().c_str()) == 0) {
                            in = true;
                            break;
                        }
                    }
                    if (COUNT >= MAX_COUNT) {
                        return;
                    }
                    if (fs::is_directory(entry.status()) && !in) {
                        COUNT++;
                        stack.push_back(entry.path());
                    }
                    {
                        filesNFolders.push_back(entry);
                        if (filesNFolders.size() >= MaxSubFolderInMemory) {
                            writeBuffer();
                        }
                    }
                }
            } catch (const fs::filesystem_error &e) {
                continue;
            }
        }
    }
}

void indexer(const fs::directory_entry &ent, rj::Document *extensionData, rj::Document *filenameData, rj::Document::AllocatorType &extensionDataAllocator, rj::Document::AllocatorType &filenameDataAllocator) {
    unique_lock<mutex> guard(indexer_mutex);
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
            loc_data = &(*loc_data)[chr.c_str()];
            if (i == fileName.size() - 1) {
                if (!loc_data->HasMember("END")) {
                    loc_data->AddMember("END", rj::Value(rj::kArrayType), extensionDataAllocator);
                }
                loc_data->FindMember("END")->value.PushBack(rj::Value().SetString(path.c_str(), extensionDataAllocator), extensionDataAllocator);
            }
        }
    }
    fileName = fileName.substr(0, ix);
    loc_data = filenameData;

    for (int i = 0; i < fileName.size(); i++) {
        if (!isalnum(fileName[i])) {
            continue;
        }
        string chr(1, tolower(fileName[i]));
        if (!loc_data->HasMember(chr.c_str())) {
            loc_data->AddMember(rj::Value(chr.c_str(), filenameDataAllocator), rj::Value(rj::kObjectType), filenameDataAllocator);
        }
        loc_data = &((*loc_data)[chr.c_str()]);
        if (i == fileName.size() - 1) {
            if (!loc_data->HasMember("END")) {
                loc_data->AddMember("END", rj::Value(rj::kArrayType), filenameDataAllocator);
            }
            loc_data->FindMember("END")->value.PushBack(rj::Value().SetString(path.c_str(), filenameDataAllocator), filenameDataAllocator);
        }
    }
}

void writeBuffer() {
    unique_lock<mutex> guard(count_mutex);
    ifstream filenameJson("../fileIndex.json", ios::in | ios::binary);
    ifstream extensionJson("../extIndex.json", ios::in | ios::binary);

    if (!filenameJson.is_open() || !extensionJson.is_open()) {
        cerr << "Error opening files for reading!" << endl;
        exit(202);
    }

    rj::Document extensionData;
    rj::Document filenameData;

    rj::Document::AllocatorType &filenameDataAllocator = filenameData.GetAllocator();
    rj::Document::AllocatorType &extensionDataAllocator = extensionData.GetAllocator();

    string extJsonContent((istreambuf_iterator<char>(extensionJson)), istreambuf_iterator<char>());
    if (!extJsonContent.empty()) {
        rj::ParseResult extParseResult = extensionData.Parse(extJsonContent.c_str());
        if (!extParseResult) {
            cerr << "Error parsing extensionJson: " << rj::GetParseErrorFunc(extParseResult.Code())
                 << " at offset " << extParseResult.Offset() << endl;
            exit(404);
        }
    } else {
        extensionData.SetObject();
    }

    string fileJsonContent((istreambuf_iterator<char>(filenameJson)), istreambuf_iterator<char>());
    if (!fileJsonContent.empty()) {
        rj::ParseResult fileParseResult = filenameData.Parse(fileJsonContent.c_str());
        if (!fileParseResult) {
            cerr << "Error parsing filenameJson: " << rj::GetParseErrorFunc(fileParseResult.Code())
                 << " at offset " << fileParseResult.Offset() << endl;
            exit(404);
        }
    } else {
        filenameData.SetObject();
    }

    filenameJson.close();
    extensionJson.close();

    for (const auto &each : filesNFolders) {
        indexer(each, &extensionData, &filenameData, extensionDataAllocator, filenameDataAllocator);
    }

    ofstream outFileFilename("../fileIndex.json", ios::out | ios::trunc | ios::binary);
    ofstream outFileExtension("../extIndex.json", ios::out | ios::trunc | ios::binary);

    if (!outFileFilename.is_open() || !outFileExtension.is_open()) {
        cerr << "Error opening files for writing!" << endl;
        exit(202);
    }

    rj::StringBuffer filenameBuffer;
    rj::Writer<rj::StringBuffer> writer1(filenameBuffer);
    filenameData.Accept(writer1);
    outFileFilename.write(filenameBuffer.GetString(), filenameBuffer.GetSize());

    rj::StringBuffer extensionBuffer;
    rj::Writer<rj::StringBuffer> writer2(extensionBuffer);
    extensionData.Accept(writer2);
    outFileExtension.write(extensionBuffer.GetString(), extensionBuffer.GetSize());

    outFileFilename.close();
    outFileExtension.close();

    filesNFolders.clear();
}