#include <algorithm>
#include <cctype>
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

// functions declarations
void helper(const vector<fs::path> &dirs);
void indexer(const fs::directory_entry &ent, rj::Document *extensionData, rj::Document *filenameData, rj::Document::AllocatorType &extensionDataAllocator, rj::Document::AllocatorType &filenameDataAllocator);
void writeBuffer();

// mutexes to protect data
mutex data_mutex;
mutex count_mutex;
mutex indexer_mutex;

// global limitations
const long MAX_COUNT = 50000000;
const int MAX_THREADS = 1;
const int MaxSubFolderInMemory = 100000;
int COUNT = 0;

// directories
deque<fs::directory_entry> filesNFolders = {};
vector<fs::path> ignoredDirectories = {R"(C:\Windows)", R"(C:\ProgramData)", R"(C:\DRIVER)", R"(C:\drivers)", R"(C:\$SysReset)", R"(C:\PerfLogs)", R"(C:\msys64)", R"(C:\vcpkg)", R"(C:\Program Files (x86)\AMD)", R"(C:\Program Files (x86)\Google)", R"(C:\Program Files (x86)\Internet Explorer)", R"(C:\Program Files (x86)\Lenovo)"};
vector<fs::path> directoriesToParse = {R"(C:\Users)"};

bool ignoreDirectories = false;  // option to set if the directories should be ignored or parse only the selected directories

int main() {
    try {
        // Set the root path to index search
        fs::path root_path = R"(C:\)";
        vector<fs::path> initial_dirs = {};

        // check if to ignore the directories
        if (ignoreDirectories) {
            for (const auto &entry : fs::directory_iterator(root_path)) {
                bool in = false;
                // iterate the ignored directories vector =
                for (auto &each : ignoredDirectories) {
                    if (strcmp(each.string().c_str(), entry.path().string().c_str()) == 0) {
                        in = true;
                        break;
                    }
                }

                // check if path is a fold and it is not in the ignoreDirectories vector
                auto status = entry.status();
                if (fs::is_directory(status) && !in) {
                    // add to initial_dirs which will be used to initialize thread to files
                    fs::path path = entry.path();
                    initial_dirs.push_back(path);
                    path.clear();
                }
            }
        } else {
            for (auto &each : directoriesToParse) {
                fs::directory_entry entry(each);
                // add the directory to the buffer to add to the json file later on
                filesNFolders.push_back(entry);
            }
            initial_dirs = directoriesToParse;
        }
        // create thread vector containing max of MAX_THREADS with assignments about which paths they will parse
        vector<vector<fs::path>> thread_dirs(MAX_THREADS);
        for (size_t i = 0; i < initial_dirs.size(); ++i) {
            // initialize the threads according to the initial_dirs
            thread_dirs[i % MAX_THREADS].push_back(initial_dirs[i]);
        }

        vector<thread> threads = {};
        chrono::steady_clock::time_point begin = chrono::steady_clock::now();

        // iterate through the thread_dirs to initialize threads; calling the helper() function with all of them
        for (int i = 0; i < MAX_THREADS; ++i) {
            if (!thread_dirs[i].empty()) {
                threads.emplace_back(helper, ref(thread_dirs[i]));
            }
        }

        // end the processes for the threads
        for (auto &t : threads) {
            t.join();
        }

        // to ensure that everything in the buffer has been processed into the jsons
        if (!filesNFolders.empty()) {
            writeBuffer();
        }
        thread_dirs.clear();
        initial_dirs.clear();
        threads.clear();
        filesNFolders.clear();

        // DEBUGGING
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
    // iterate through the directories for a thread
    for (const auto &dir : dirs) {
        // keeping a queue for bfs approach for going through the directories
        deque<fs::path> stack = {};
        stack.push_back(dir);

        while (!stack.empty()) {
            // get the firectory
            fs::path current_dir = stack.front();
            stack.pop_front();

            try {
                // lock the mutex to protect its data from mixing with other threads
                unique_lock<mutex> guard(data_mutex);
                for (const auto &entry : fs::directory_iterator(current_dir)) {
                    // bool for the path in ignoredDirectories
                    bool in = false;
                    // ---> process for ignoring the ignoredDirectories vector
                    if (ignoreDirectories) {
                        for (auto &each : ignoredDirectories) {
                            if (strcmp(each.string().c_str(), entry.path().string().c_str()) == 0) {
                                in = true;
                                break;
                            }
                        }
                    }
                    // <---->

                    // add path to the buffer to be written to the json later on
                    filesNFolders.push_back(entry);

                    // if the has reached desired size write it to json
                    if (filesNFolders.size() >= MaxSubFolderInMemory) {
                        writeBuffer();
                    }

                    // DEBUGGING -- limiting the amount of files parsed for now for testing purposes
                    if (fs::is_directory(entry.status()) && !in) {
                        COUNT++;
                        stack.push_back(entry.path());
                    }

                    if (COUNT >= MAX_COUNT) {
                        return;
                    }
                }
            } catch (const fs::filesystem_error &e) {  // if the path cant be opened, skip it
                continue;
            }
            this_thread::sleep_for(chrono::nanoseconds(10000));
        }
        stack.clear();
    }
}

void indexer(const fs::directory_entry &ent, rj::Document *extensionData, rj::Document *filenameData, rj::Document::AllocatorType &extensionDataAllocator, rj::Document::AllocatorType &filenameDataAllocator) {
    // lock mutex to protect data
    unique_lock<mutex> guard(indexer_mutex);

    const fs::path &pathh = ent.path();

    const string &path = ent.path().string();
    // find the file name --> C:/folder/folder1/file, fileName = file
    string fileName = path.substr(path.find_last_of('\\') + 1);

    // rapidjson Value a document buffer for json file
    rj::Value *loc_data = nullptr;

    // check if it is not a directory -- string::npos is to ensure it has a extension
    if (!fs::is_directory(ent.status()) && pathh.has_extension()) {
        // point loc_data to extensionData (holds our extension json)
        loc_data = extensionData;

        // initiliaze the first path folder
        size_t oldFind = 0;
        size_t newFind = path.find('\\', oldFind + 1);

        while (newFind != string::npos) {
            string ppp = path.substr(oldFind, newFind - oldFind) + '/';

            const char *pth = ppp.c_str();
            if (!loc_data->HasMember(pth)) {
                // add a member to the extensionData with chr as the key
                rj::Value val(pth, extensionDataAllocator);
                rj::Value obj(rj::kObjectType);
                loc_data->AddMember(val, obj, extensionDataAllocator);
            }
            // point the loc_data with the newly initialized member or the old one
            loc_data = &(*loc_data)[pth];
            oldFind = newFind + 1;
            newFind = path.find('\\', oldFind);
        }
        string cr = pathh.extension().string();
        // start iterating through the extension name
        for (int i = 1; i < cr.size(); i++) {
            string chr(1, tolower(cr[i]));
            // make sure the character is valid utf-8 and it is not a symbol (for convenience and simplicity)
            if (!isalnum(cr[i])) {
                continue;
            }
            // check if the loc_data already has a index with that character if not add the member to the extensionData
            if (!loc_data->HasMember(chr.c_str())) {
                // add a member to the extensionData with chr as the key
                rj::Value val(chr.c_str(), extensionDataAllocator);
                rj::Value obj(rj::kObjectType);
                loc_data->AddMember(val, obj, extensionDataAllocator);
            }
            // point the loc_data with the newly initialized member or the old one
            loc_data = &(*loc_data)[chr.c_str()];

            // if reached the end of fileName then add a key called END to indicate that this file has ended which will be used to search for file
            if (i == cr.size() - 1) {
                // check if the loc_data already has a index with that character if not add the member to the extensionData
                if (!loc_data->HasMember("END")) {
                    rj::Value arr(rj::kArrayType);
                    loc_data->AddMember("END", arr, extensionDataAllocator);
                }
                rj::Value str(fileName.c_str(), extensionDataAllocator);
                // append the fileName to the "END" key in the document
                loc_data->FindMember("END")->value.PushBack(str, extensionDataAllocator);
            }
        }
    }

    // find file name before the extension
    string fileName1 = pathh.stem().string();

    // point teh loc_data to teh json fileName document
    loc_data = filenameData;

    // initiliaze the first path folder
    size_t oldFind = 0;
    size_t newFind = path.find('\\', oldFind + 1);

    while (newFind != string::npos) {
        string ppp = path.substr(oldFind, newFind - oldFind) + '/';
        const char *pth = ppp.c_str();
        if (!loc_data->HasMember(pth)) {
            // add a member to the extensionData with chr as the key
            rj::Value val(pth, extensionDataAllocator);
            rj::Value obj(rj::kObjectType);
            loc_data->AddMember(val, obj, extensionDataAllocator);
        }
        // point the loc_data with the newly initialized member or the old one
        loc_data = &(*loc_data)[pth];
        oldFind = newFind + 1;
        newFind = path.find('\\', oldFind);
    }

    for (int i = 0; i < fileName1.size(); i++) {
        string chr(1, tolower(fileName1[i]));
        // make sure the character is valid utf-8 and it is not a symbol (for convenience and simplicity)
        if (!isalnum(fileName1[i])) {
            continue;
        }
        // check if the loc_data already has a index with that character if not add the member to the extensionData
        if (!loc_data->HasMember(chr.c_str())) {
            // add a member to the extensionData with chr as the key
            rj::Value val(chr.c_str(), filenameDataAllocator);
            rj::Value obj(rj::kObjectType);
            loc_data->AddMember(val, obj, filenameDataAllocator);
        }
        // point the loc_data with the newly initialized member or the old one
        loc_data = &(*loc_data)[chr.c_str()];

        // if reached the end of fileName then add a key called END to indicate that this file has ended which will be used to search for file
        if (i == fileName1.size() - 1) {
            // check if the loc_data already has a index with that character if not add the member to the extensionData
            if (!loc_data->HasMember("END")) {
                rj::Value arr(rj::kArrayType);
                loc_data->AddMember("END", arr, filenameDataAllocator);
            }
            rj::Value str(fileName.c_str(), filenameDataAllocator);
            // append the fileName to the "END" key in the document
            loc_data->FindMember("END")->value.PushBack(str, filenameDataAllocator);
        }
    }
    loc_data = nullptr;
}

void writeBuffer() {
    // loack mutex to guard data
    unique_lock<mutex> guard(count_mutex);
    // open files
    ifstream filenameJson("../fileIndex.json", ios::in | ios::binary);
    ifstream extensionJson("../extIndex.json", ios::in | ios::binary);

    if (!filenameJson.is_open() || !extensionJson.is_open()) {
        cerr << "Error opening files for reading!" << endl;
        exit(202);
    }
    // initialize extensionData and filenameData json file documents
    rj::Document extensionData;
    rj::Document filenameData;

    // allocators that are used for create members
    rj::Document::AllocatorType &filenameDataAllocator = filenameData.GetAllocator();
    rj::Document::AllocatorType &extensionDataAllocator = extensionData.GetAllocator();

    // read the entire content of the extensionJson file into a string
    string extJsonContent((istreambuf_iterator<char>(extensionJson)), istreambuf_iterator<char>());

    // if not empty parse the json with c_str
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
    extJsonContent.clear();

    // read the entire content of the extensionJson file into a string
    string fileJsonContent((istreambuf_iterator<char>(filenameJson)), istreambuf_iterator<char>());

    // if not empty parse the json with c_str
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

    fileJsonContent.clear();

    // close files
    filenameJson.close();
    extensionJson.close();

    // index each path in filesNfolders buffer
    for (const auto &each : filesNFolders) {
        indexer(each, &extensionData, &filenameData, extensionDataAllocator, filenameDataAllocator);
    }

    // open files as output stream with truncation
    ofstream outFileFilename("../fileIndex.json", ios::out | ios::trunc | ios::binary);
    ofstream outFileExtension("../extIndex.json", ios::out | ios::trunc | ios::binary);

    if (!outFileFilename.is_open() || !outFileExtension.is_open()) {
        cerr << "Error opening files for writing!" << endl;
        exit(202);
    }

    // write into the files
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

    // release buffer
    filesNFolders.clear();
}