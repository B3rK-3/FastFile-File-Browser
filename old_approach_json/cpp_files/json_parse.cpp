#include <iostream>
#include <queue>
#include <regex>
#include <vector>

#include "../libraries/simdjson.h"

namespace json = simdjson;
using namespace std;

vector<string> searchEnds(json::ondemand::object& data);

int main() {
    json::ondemand::parser parser;
    json::padded_string jsonFile;
    string userSearch = "";
    string searchDir = "";

    cout << "What is the path of the directory? >>";
    getline(cin, searchDir);

    cout << "\nWhat file to search for? Hint: put a `.` at the start to search for extension >>";
    getline(cin, userSearch);

    bool extensionSearch = userSearch[0] == '.' ? true : false;
    string file = extensionSearch ? "../extIndex.json" : "../fileIndex.json";

    try {
        jsonFile = json::padded_string::load(file);
    } catch (json::simdjson_error& e) {
        cout << e.error();
        return 0;
    }

    // Parse the JSON file
    json::ondemand::document content = parser.iterate(jsonFile);
    json::ondemand::object data = content.get_object();

    // Normalize search directory path
    regex rep("\\\\");
    searchDir = regex_replace(searchDir, rep, "/");

    size_t oldFind = 0;
    size_t newFind = searchDir.find("/", oldFind + 1);

    while (newFind != string::npos) {
        string pth = searchDir.substr(oldFind, newFind - oldFind) + '/';
        cout << pth;

        // Correctly retrieve the nested object
        auto result = data[pth].get_object();
        if (result.error()) {
            cout << result.error() << "\n" + searchDir + " directory invalid or not indexed!";
            exit(result.error());
        }

        // Update the data object with the newly retrieved object
        auto resresult.get(data);

        oldFind = newFind + 1;
        newFind = searchDir.find("/", oldFind);
    }

    cout << data;
    /*
        tuple<json::ondemand::object, string> firstTuple = make_tuple(data, userSearch.substr(0, userSearch.find_last_of('/')));
        queue<tuple<json::ondemand::object, string>> q;
        q.push(firstTuple);
        vector<string> results;
        bool found = false;

        while (!q.empty()) {
            tuple<json::ondemand::object, string> nd = q.front();
            q.pop();
            json::ondemand::object node = get<0>(nd);
            string path = get<1>(nd);

            for (json::ondemand::field element : node) {
                string key = std::string(element.key().raw());

                // Debugging output (can be removed in production)
                cout << key << " --- " << typeid(key).name() << endl << string("-", 100) << endl;

                auto value = element.value();

                // Check if the current key is a directory (ending with '/')
                if (key.back() == '/' && value.type() == json::ondemand::json_type::object) {
                    q.push(make_tuple(value.get_object(), path + key));
                    continue;
                }

                // Handle "END" key by collecting results from the array
                if (key == "END" && value.type() == json::ondemand::json_type::array) {
                    for (auto each : value.get_array()) {
                        results.push_back(std::string(each.get_string().value_unsafe()));
                    }
                    continue;
                }

                // Isolate the last component of the path + key
                size_t lastSlashPos = path.find_last_of('/');
                string lastComponent = (lastSlashPos != string::npos && lastSlashPos != path.length() - 1)
                                       ? path.substr(lastSlashPos + 1) + key
                                       : key;

                // Compare the prefix for a match
                int minLength = min(lastComponent.length(), userSearch.length());
                if (userSearch.substr(0, minLength) == lastComponent.substr(0, minLength)) {
                    if (value.type() == json::ondemand::json_type::object) {
                        q.push(make_tuple(value.get_object(), path + key));
                    }
                }
            }
        }

        if (results.empty()) {
            cout << "No matches found." << endl;
        } else {
            for (const auto& each : results) {
                cout << each << " ";
            }
            cout << endl;
        }*/

    return 0;
}
