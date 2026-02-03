// Convert images sheets to dropbox.cpp : This file contains the 'main' function. Program execution begins and ends there.

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <curl/curl.h>
#include <iomanip>
#include "Configuration.h"

using namespace std;

string urlEncode(const string& value) {
    ostringstream escaped;
    escaped.fill('0');
    escaped << hex;
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        }
        else {
            escaped << '%' << setw(2) << int((unsigned char)c);
        }
    }
    return escaped.str();
}

size_t writeToString(void* ptr, size_t size, size_t nmemb, void* userdata) {
    string* data = static_cast<string*>(userdata);
    data->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

string downloadCSV() {

    cout << "URL = [" << SHEETS_CSV_URL << "]" << endl;

    CURL* curl = curl_easy_init();

    string csvData;

    curl_easy_setopt(curl, CURLOPT_URL, SHEETS_CSV_URL.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &csvData);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        cerr << "Curl error: " << curl_easy_strerror(res) << endl;
        return "";
    }

    return csvData;
}

vector<string> extractColumn(const string& csv, int columnIndex) {
    vector<string> column;
    stringstream ss(csv);
    string line;

    while (getline(ss, line)) {
        stringstream lineStream(line);
        string cell;

        for (int i = 0; i <= columnIndex; i++) {
            if (!getline(lineStream, cell, ',')) {
                cell = "";
                break;
            }
        }

        column.push_back(cell);
    }

    return column;
}

string trim(const string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    if (start == string::npos) return "";
    return s.substr(start, end - start + 1);
}

bool needsProcessing(const string& cell) {
    string c = trim(cell);

    if (c.empty())
        return false;

    if (c.find("dropbox.com") != string::npos)
        return false;

    return c.rfind("http", 0) == 0;
}

size_t writeTextCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), total);
    return total;
}

size_t writeBinaryCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    string* buffer = static_cast<string*>(userp);
    buffer->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

void addDropboxBusinessHeaders(
    struct curl_slist*& headers,
    const string& accessToken
) {
    headers = curl_slist_append(headers,
        ("Authorization: Bearer " + accessToken).c_str());

    headers = curl_slist_append(headers,
        ("Dropbox-API-Select-User: " +
            string(DROPBOX_TEAM_MEMBER_ID)).c_str());
}

void addDropboxNamespaceHeader(struct curl_slist*& headers) {
    // JSON, no escaping, Dropbox requires this exact shape
    string pathRoot =
        "{\".tag\":\"namespace_id\",\"namespace_id\":\"" +
        string(DROPBOX_NAMESPACE_ID) + "\"}";

    headers = curl_slist_append(
        headers,
        ("Dropbox-API-Path-Root: " + pathRoot).c_str()
    );
}

bool downloadImage(const string& imageUrl, string& imageData)
{
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, imageUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBinaryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &imageData);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return res == CURLE_OK && !imageData.empty();
}

bool uploadToDropbox(
    const string& accessToken,
    const string& fileData,
    const string& dropboxPath,
    string& responseOut)
{
    //cerr << "[uploadToDropbox] ENTER, path=" << dropboxPath << ", file size=" << fileData.size() << endl;

    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "[uploadToDropbox] CURL init failed\n";
        return false;
    }

    struct curl_slist* headers = nullptr;

    string apiArg =
        "{\"path\":\"" + dropboxPath +
        "\",\"mode\":\"add\",\"autorename\":true,\"mute\":false}";

    addDropboxBusinessHeaders(headers, accessToken);
    addDropboxNamespaceHeader(headers);

    headers = curl_slist_append(headers,
        "Content-Type: application/octet-stream");

    headers = curl_slist_append(headers,
        ("Dropbox-API-Arg: " + apiArg).c_str());


    curl_easy_setopt(curl, CURLOPT_URL,
        "https://content.dropboxapi.com/2/files/upload");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, fileData.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, fileData.size());
    responseOut.clear();
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeTextCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseOut);


    CURLcode res = curl_easy_perform(curl);


    if (!responseOut.empty()) {
        //  cerr << "[uploadToDropbox] Dropbox response: " << responseOut << endl;
    }
    else {
        //  cerr << "[uploadToDropbox] Dropbox response empty\n";
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    bool success = (res == CURLE_OK);
    // cerr << "[uploadToDropbox] EXIT, success=" << success << endl;
    return success;
}

bool createDropboxShareLink(
    const string& accessToken,
    const string& dropboxPath,
    string& outLink)
{
    cerr << "[createDropboxShareLink] ENTER, path=" << dropboxPath << endl;

    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "[createDropboxShareLink] CURL init failed\n";
        return false;
    }


    struct curl_slist* headers = nullptr;

    addDropboxBusinessHeaders(headers, accessToken);
    addDropboxNamespaceHeader(headers);

    headers = curl_slist_append(headers,
        "Content-Type: application/json");


    string jsonPayload = "{\"path\":\"" + dropboxPath + "\",\"settings\":{\"requested_visibility\":\"public\"}}";

    string response;
    curl_easy_setopt(curl, CURLOPT_URL,
        "https://api.dropboxapi.com/2/sharing/create_shared_link_with_settings");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonPayload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBinaryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    // cerr << "[createDropboxShareLink] after perform, res=" << res << endl;
    if (!response.empty()) {
        //  cerr << "[createDropboxShareLink] Dropbox response: " << response << endl;
    }
    else {
        // cerr << "[createDropboxShareLink] Dropbox response empty\n";
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        // cerr << "[createDropboxShareLink] CURL failed: " << curl_easy_strerror(res) << endl;
        return false;
    }

    size_t urlPos = response.find("\"url\":");
    if (urlPos == string::npos) {
        cerr << "[createDropboxShareLink] URL not found in response\n";
        return false;
    }

    size_t start = response.find("\"", urlPos + 6);
    size_t end = response.find("\"", start + 1);
    if (start == string::npos || end == string::npos) {
        cerr << "[createDropboxShareLink] URL parse failed\n";
        return false;
    }

    string url = response.substr(start + 1, end - start - 1);
    size_t dlPos = url.find("?dl=0");
    if (dlPos != string::npos)
        url.replace(dlPos, 5, "?dl=1");

    outLink = url;
    //cerr << "[createDropboxShareLink] EXIT, outLink=" << outLink << endl;
    return true;
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

string getExistingSharedLink(const string& accessToken, const string& dropboxPath) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    string readBuffer;

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.dropboxapi.com/2/sharing/list_shared_links");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    string jsonData = "{\"path\":\"" + dropboxPath + "\", \"direct_only\": true}";

    struct curl_slist* headers = nullptr;

    addDropboxBusinessHeaders(headers, accessToken);
    addDropboxNamespaceHeader(headers);

    headers = curl_slist_append(headers,
        "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        cerr << "Error fetching existing shared link: " << curl_easy_strerror(res) << endl;
        return "";
    }


    size_t pos = readBuffer.find("\"url\":");
    if (pos != string::npos) {
        size_t start = readBuffer.find("\"", pos + 6) + 1;
        size_t end = readBuffer.find("\"", start);
        if (start != string::npos && end != string::npos) {
            return readBuffer.substr(start, end - start);
        }
    }

    return "";
}

string extractPathLower(const string& response) {
    size_t pos = response.find("\"path_lower\":");
    if (pos == string::npos) return "";

    size_t start = response.find("\"", pos + 13) + 1;
    size_t end = response.find("\"", start);
    if (start == string::npos || end == string::npos) return "";

    return response.substr(start, end - start);
}

string getDropboxAccessToken() {
    //string dropboxAccessToken = getDropboxAccessToken();
    //cerr << "[DEBUG] Dropbox token: " << dropboxAccessToken << endl;  // <--- add this
   //if (dropboxAccessToken.empty()) {
        //cerr << "Failed to obtain Dropbox access token\n";
        //return 1; }

    CURL* curl = curl_easy_init();
    if (!curl) return "";

    string response;

    string postData =
        "grant_type=refresh_token&refresh_token=" + urlEncode(DROPBOX_REFRESH_TOKEN) +
        "&client_id=" + urlEncode(DROPBOX_APP_KEY) +
        "&client_secret=" + urlEncode(DROPBOX_APP_SECRET);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.dropboxapi.com/oauth2/token");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // cout << "[DEBUG] Dropbox token response: " << response << endl;

    if (res != CURLE_OK) {
        cerr << "[ERROR] Curl failed: " << curl_easy_strerror(res) << endl;
        return "";
    }

    // Look for "access_token" key in the JSON response
    size_t pos = response.find("\"access_token\":");
    if (pos == string::npos) {
        cerr << "[ERROR] access_token not found in response" << endl;
        return "";
    }

    // Skip past "access_token":
    pos += 15;

    // Skip any spaces and quotes
    while (pos < response.size() && (response[pos] == ' ' || response[pos] == '\"'))
        ++pos;

    size_t end = response.find("\"", pos);
    if (end == string::npos) {
        cerr << "[ERROR] access_token parse failed" << endl;
        return "";
    }

    string token = response.substr(pos, end - pos);
    //cout << "[DEBUG] Access token extracted: " << token << endl;
    return token;
}

string getGoogleAccessToken() {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    string response;

    string postData =
        "client_id=" + urlEncode(GOOGLE_CLIENT_ID) +
        "&client_secret=" + urlEncode(GOOGLE_CLIENT_SECRET) +
        "&refresh_token=" + urlEncode(GOOGLE_REFRESH_TOKEN) +
        "&grant_type=refresh_token";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        cerr << "[ERROR] Curl failed: " << curl_easy_strerror(res) << endl;
        return "";
    }

    //cout << "[DEBUG] Google token response: " << response << endl;  // ← debug

    // Parse access_token from JSON response
    size_t pos = response.find("\"access_token\":");
    if (pos == string::npos) {
        cerr << "[ERROR] access_token not found in response" << endl;
        return "";
    }
    pos += 15;
    while (pos < response.size() && (response[pos] == ' ' || response[pos] == '\"')) ++pos;
    size_t end = response.find("\"", pos);
    if (end == string::npos) {
        cerr << "[ERROR] access_token parse failed" << endl;
        return "";
    }

    return response.substr(pos, end - pos);
}

bool updateSheetCell(int row, int col, const string& value, const string& accessToken) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    // Make sure the tab is "Images"
    string url = "https://sheets.googleapis.com/v4/spreadsheets/" + string(GOOGLE_SHEET_ID)
        + "/values/Images!R" + to_string(row) + "C" + to_string(col)
        + "?valueInputOption=RAW";

    string jsonPayload = "{\"values\":[[\"" + value + "\"]]}";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + accessToken).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonPayload.c_str());

    string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        cerr << "[ERROR] Sheet update failed: " << curl_easy_strerror(res) << endl;
        return false;
    }

    //cout << "[DEBUG] Sheet update response: " << response << endl;  // debug

    return true;
}

bool dropboxLinkMatchesFilename(const string& dropboxLink, const string& fileName) {
    if (dropboxLink.empty() || fileName.empty())
        return false;

    return dropboxLink.find(fileName) != string::npos;
}

vector<string> listTeamMemberIds(const string& accessToken) {
    vector<string> ids;
    CURL* curl = curl_easy_init();
    if (!curl) return ids;

    string response;
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.dropboxapi.com/2/team/members/list");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + accessToken).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "{}"); // empty body
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        cerr << "Failed to list team members: " << curl_easy_strerror(res) << endl;
        return ids;
    }

    // Parse team_member_id dynamically
    size_t pos = 0;
    while ((pos = response.find("\"team_member_id\":", pos)) != string::npos) {
        size_t start = response.find("\"", pos + 17) + 1;
        size_t end = response.find("\"", start);
        if (start != string::npos && end != string::npos) {
            ids.push_back(response.substr(start, end - start));
            pos = end;
        }
        else break;
    }

    return ids;
}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Dropbox folder required\n";
        return 1;
    }

    std::string DROPBOX_FOLDER = argv[1];
    if (DROPBOX_FOLDER.back() != '/')
        DROPBOX_FOLDER += '/';

    curl_global_init(CURL_GLOBAL_DEFAULT);

    // --- Dropbox access token ---
    string dropboxAccessToken = getDropboxAccessToken();
    if (dropboxAccessToken.empty()) {
        cerr << "Failed to obtain Dropbox access token\n";
        return 1;
    }
    vector<string> teamMembers = listTeamMemberIds(dropboxAccessToken);
    if (teamMembers.empty()) {
        cerr << "No team members found!\n";
        return 1;
    }

    // --- Download CSV ---
    string csvRaw = downloadCSV();
    if (csvRaw.empty()) {
        cerr << "Failed to download CSV\n";
        return 1;
    }

    // --- Extract columns ---
    vector<string> imageUrls = extractColumn(csvRaw, IMAGE_COLUMN_INDEX);
    vector<string> fileNames = extractColumn(csvRaw, FILENAME_COLUMN_INDEX);

    // --- Parse CSV into 2D vector ---
    vector<vector<string>> csvData;
    string line;
    stringstream ss(csvRaw);
    while (getline(ss, line)) {
        stringstream lineStream(line);
        string cell;
        vector<string> row;
        while (getline(lineStream, cell, ',')) row.push_back(cell);
        csvData.push_back(row);
    }

    // --- Google access token ---
    string googleAccessToken = getGoogleAccessToken();
    if (googleAccessToken.empty()) {
        cerr << "Failed to get Google access token\n";
        return 1;
    }

    // --- Process each row ---
    for (size_t i = 1; i < imageUrls.size(); ++i) {
        string cell = trim(imageUrls[i]);
        string existingLink =
            (i < csvData.size() && csvData[i].size() > 1)
            ? trim(csvData[i][1])
            : "";

        // Expected filename (Column C)
        string expectedFileName =
            (i < fileNames.size() && !fileNames[i].empty())
            ? fileNames[i]
            : "image_" + to_string(i + 1) + ".jpg";

        if (expectedFileName.find('.') == string::npos)
            expectedFileName += ".jpg";

        // Skip ONLY if Dropbox link already matches filename
        if (!existingLink.empty() &&
            existingLink.find("dropbox.com") != string::npos &&
            dropboxLinkMatchesFilename(existingLink, expectedFileName))
        {
            cout << "Skipping row " << i + 2 << " (Dropbox link matches filename)" << endl;
            continue;
        }

        // Validate image URL (replaces needsProcessing)
        if (cell.empty() || cell.rfind("http", 0) != 0) {
            //cout << "Skipping row " << i + 2 << " (invalid image URL)" << endl;
            continue;
        }

        // --- Download image ---
        string imageData;
        if (!downloadImage(cell, imageData)) {
            cerr << "Failed to download image from " << cell << endl;
            continue;
        }

        // --- Determine file name ---
        string fileName = (i < fileNames.size() && !fileNames[i].empty())
            ? fileNames[i]
            : "image_" + to_string(i + 1) + ".jpg";
        if (fileName.find('.') == string::npos) fileName += ".jpg";

        string dropboxPath = DROPBOX_FOLDER + fileName;
        string dropboxResponse;

        //cerr << "[DEBUG] Using team member ID: " << DROPBOX_TEAM_MEMBER_ID << endl;
       // cerr << "[DEBUG] Using namespace ID: " << DROPBOX_NAMESPACE_ID << endl;

        // --- Upload to Dropbox ---
        if (!uploadToDropbox(dropboxAccessToken, imageData, dropboxPath, dropboxResponse)) {
            cerr << "Upload failed for " << fileName << endl;
            continue;
        }

        string actualPath = extractPathLower(dropboxResponse);
        if (actualPath.empty()) {
            cerr << "Failed to extract Dropbox path for " << fileName << endl;
            continue;
        }

        cout << "Uploaded " << fileName << endl;

        // --- Get or create shared link ---
        string dropboxLink = getExistingSharedLink(dropboxAccessToken, actualPath);
        if (dropboxLink.empty() && !createDropboxShareLink(dropboxAccessToken, actualPath, dropboxLink)) {
            cerr << "Failed to create shared link for " << fileName << endl;
            continue;
        }

        cout << "Dropbox link: " << dropboxLink << endl;

        // --- Update CSV data locally ---
        if (i < csvData.size()) {
            csvData[i].resize(max<size_t>(3, csvData[i].size()));
            csvData[i][1] = dropboxLink; // Column B
        }

        // --- Update Google Sheet ---
        if (!updateSheetCell(i + 1, 2, dropboxLink, googleAccessToken)) {
            cerr << "Failed to update Google Sheet for row " << i + 1 << endl;
        }
    }

    // --- Print updated CSV ---
    cout << "\nUpdated CSV:\n";
    for (auto& row : csvData) {
        for (size_t j = 0; j < row.size(); ++j) {
            cout << row[j];
            if (j + 1 < row.size()) cout << ",";
        }
        cout << "\n";
    }

    curl_global_cleanup();
    return 0;
}
