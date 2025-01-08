#include <iostream>
#include <queue>
#include <regex>
#include <vector>
#include <filesystem>

#include "../libraries/yyjson.h"

using namespace std;
namespace fs = filesystem;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: file_searcher <directory_path> <search_term>" << endl;
        return 1;
    }

    string searchDir = argv[1];
    string userSearch = argv[2];

    bool extensionSearch = false;
    if (userSearch[0] == '.') {
        userSearch = userSearch.substr(1);
        extensionSearch = true;
    }
    string file = extensionSearch ? "C:/Users/josbu/OneDrive/Documents/GitHub/test_app/extIndex.json" : "C:\\Users\\josbu\\OneDrive\\Documents\\GitHub\\test_app\\fileIndex.json";

    // Allocate a buffer for reading the file
    FILE* fp = fopen(file.c_str(), "rb");
    if (!fp) {
        cerr << "Failed to open JSON file" << ' ' + file << endl;
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    size_t filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* buffer = new char[filesize];
    fread(buffer, 1, filesize, fp);
    fclose(fp);

    yyjson_doc* doc = yyjson_read(buffer, filesize, 0);
    if (!doc) {
        cerr << "Failed to load JSON file" << endl;
        delete[] buffer;
        return 1;
    }

    // Get the root object
    yyjson_val* data = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(data)) {
        cerr << "Root is not a JSON object" << endl;
        yyjson_doc_free(doc);
        delete[] buffer;
        return 1;
    }

    // Normalize search directory path
    regex rep("\\\\");
    searchDir = regex_replace(searchDir, rep, "/");
    if (searchDir.back() != '/') {
        searchDir += '/';
    }

    // Traverse the JSON structure according to the directory path
    size_t oldFind = 0;
    size_t newFind = searchDir.find("/", oldFind + 1);
    bool found = true;

    while (newFind != string::npos) {
        string pth = searchDir.substr(oldFind, newFind - oldFind + 1);  // Get the current directory segment
        found = false;

        yyjson_val* next_obj = yyjson_obj_get(data, pth.c_str());
        if (next_obj && yyjson_is_obj(next_obj)) {
            data = next_obj;  // Move to the nested object
            found = true;
        }

        if (!found) {
            cout << "Directory " << pth << " not found or not indexed!" << endl;
            yyjson_doc_free(doc);
            delete[] buffer;
            return 1;
        }

        oldFind = newFind + 1;
        newFind = searchDir.find("/", oldFind);
    }

    queue<tuple<yyjson_val*, string>> q;
    q.push(make_tuple(data, searchDir));

    cout << "\n-----Results-----\n";
    while (!q.empty()) {
        auto [node, path] = q.front();
        q.pop();

        yyjson_val *key, *value;
        size_t idx, max;

        yyjson_obj_foreach(node, idx, max, key, value) {
            string key_str = yyjson_get_str(key);
            // Handle directories
            if (key_str.back() == '/' && yyjson_is_obj(value)) {
                q.push(make_tuple(value, path + key_str));
                continue;
            }

            // Handle "END" key
            if (key_str == "END" && yyjson_is_arr(value) && userSearch.length() <= path.length()-path.find_last_of('/')-1) {
                yyjson_val* each;
                size_t arr_idx, arr_max;

                yyjson_arr_foreach(value, arr_idx, arr_max, each) {
                    cout << path.substr(0, path.find_last_of('/')+1) + string(yyjson_get_str(each)) + '\n';
                    cout.flush();
                }
                continue;
            }

            // Isolate the last component of the path + key
            size_t lastSlashPos = path.find_last_of('/');
            string lastComponent = (lastSlashPos != string::npos && lastSlashPos != path.length() - 1)
                                       ? path.substr(lastSlashPos + 1) + key_str
                                       : key_str;

            // Compare the prefix for a match
            int minLength = min(lastComponent.length(), userSearch.length());
            if (userSearch.substr(0, minLength) == lastComponent.substr(0, minLength)) {
                if (yyjson_is_obj(value)) {
                    q.push(make_tuple(value, path + key_str));
                }
            }
        }
    }

    // Clean up
    yyjson_doc_free(doc);
    delete[] buffer;

    return 0;
}