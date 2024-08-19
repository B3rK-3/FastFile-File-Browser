#include <iostream>

#include "../libraries/simdjson.h"
namespace json = simdjson;
using namespace std;

int main() {
    json::ondemand::parser parser;
    json::padded_string jsonFile;
    string userSearch;

    cout << "What file to search for? Hint: put a `.` at the start to search for extension >>";
    getline(cin, userSearch);

    bool extensionSearch = userSearch[0] == '.' ? true : false;
    string file;
    if (!extensionSearch) {
        file = "../fileIndex.json";
    } else {
        file = "../extIndex.json";
    }

    try {
        jsonFile = json::padded_string::load(file);
    } catch (json::simdjson_error& e) {
        cout << e.error();
        return 0;
    }

    // Parse the JSON file
    json::ondemand::document content = parser.iterate(jsonFile);

    json::ondemand::object data = content;
    json::ondemand::array lastEnd;

    bool found = false;
    for (int i = 0; i < userSearch.size(); i++) {
        if (isalnum(userSearch[i])) {
            string chr(1, tolower(userSearch[i]));
            auto er = data[chr].get_object().get(data);
            // if (er) {break;} can be added dont want to get AMD results after typing AM0D
            auto err = data["END"].get_array().get(lastEnd);
            if (!err) {
                found = true;
            }
        }
    }

    if (found && !lastEnd.is_empty()) {
        cout << "Last found files were: " << lastEnd;
    }

    return 0;
}