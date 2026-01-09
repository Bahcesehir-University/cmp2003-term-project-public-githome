// This is our main place, like theengine room, here we take data from CSV and rank it.

#include "analyzer.h"
#include <fstream>
#include <algorithm>
#include <cctype>
using namespace std;

// HELPERS AND PARSING FUNCTIONS

// Clean up r
static inline void stripTrailingCR(string &s)
{
    if (!s.empty() && s.back() == '\r')
        s.pop_back();
}

// A little check of a the data row, we shouldn't waste time attempting to parse it if there aren't at least five commas because there are missing columns.
static inline int countCommas(const char *s)
{
    int commas = 0;
    for (const char *p = s; *p; ++p)
        if (*p == ',')
            ++commas;
    return commas;
}

// Digs into the "YYYY-MM-DD HH:MM" string to find JUST the hour.
static inline int parseHourFromDatetime(const char *p)
{
    // move to the space between date and time
    while (*p && *p != ' ' && *p != ',')
        ++p;

    // If we hit a comma here or the end of the line before finding a space, the format is wrong.
    if (!*p || *p == ',')
        return -1;

    // skip space to get HH part.
    while (*p == ' ')
        ++p;

    // now at HH:MM
    if (!isdigit(static_cast<unsigned char>(*p)))
        return -1;

    int hour = 0;
    while (isdigit(static_cast<unsigned char>(*p)))
    {
        hour = hour * 10 + (*p - '0');
        ++p;
    }
    // check hour range
    if (hour < 0 || hour > 23)
        return -1;
    return hour;
}

// Extract PickupZoneID (column 1) and hour(from column 3) from a 6-column data row.
// Returns true if the row is usable, false if it should be skipped with the main Slicer Grgabs the zone ID and the hour from a single line of the data.
static bool parseRow6(const string &line, string &zoneOut, int &hourOut)
{
    const char *buf = line.c_str();
    const char *p = buf;

    // Quick check for malformed rows.
    // Validation space
    if (countCommas(buf) < 5)
        return false;

    // 1Skip Column 0 the (TripID) by running until we hit the first other comma.
    while (*p && *p != ',')
        ++p;
    if (!*p)
        return false;
    ++p; // skip comma

    // 2Extract column 1 PickupZoneID
    while (*p == ' ' || *p == '\t')
        ++p;
    const char *zoneStart = p;
    while (*p && *p != ',')
        ++p;
    if (!*p)
        return false;

    // A clean up trailing spaces
    const char *zoneEnd = p;
    while (zoneEnd > zoneStart && (zoneEnd[-1] == ' ' || zoneEnd[-1] == '\t'))
        --zoneEnd;
    if (zoneStart == zoneEnd)
        return false;

    zoneOut.assign(zoneStart, zoneEnd - zoneStart);
    ++p; // skip comma

    // 3We skip Column 2 the (DropoffZoneID) and then we don't need it for this analysis.
    while (*p && *p != ',')
        ++p;
    if (!*p)
        return false;
    ++p;

    // 4Parse the hour from Column 3 the (PickupDateTime).
    hourOut = parseHourFromDatetime(p);
    if (hourOut < 0)
        return false;

    return true; // success
}

// TRIP ANALYZER PART

void TripAnalyzer::ingestFile(const std::string &csvPath)
{
    ifstream file(csvPath);
    if (!file.is_open())
        return;

    // Tell the maps to clear out some space early so they don't have to rehash so often.
    //  Reserve to reduce rehashing on large inputs.
    zoneCount.reserve(100000);
    slotCount.reserve(500000);

    string line;
    bool headerHandled = false;

    while (getline(file, line))
    {
        stripTrailingCR(line);
        if (line.empty())
            continue;

        // Skip the very first line if it contains TripID (the headers of it).
        if (!headerHandled)
        {
            headerHandled = true;

            if (line.find("TripID") != string::npos)
                continue;

            // Backup check: if first field isn't a digit, it's probably a header row
            const char *p = line.c_str();
            while (*p == ' ' || *p == '\t')
                ++p;
            if (!isdigit(static_cast<unsigned char>(*p)))
                continue;

            // else: treat first line as data (fall through to parse)
        }

        string zone;
        int hour = -1;

        // Pull the data we need out of the line.
        if (!parseRow6(line, zone, hour))
            continue;

        // tally things up:
        zoneCount[zone] += 1;         // This zone just got another trip.
        slotCount[{zone, hour}] += 1; // This specific zone at this specific hour just got another trip to tally.
    }
}

std::vector<ZoneCount> TripAnalyzer::topZones(int k) const
{
    vector<ZoneCount> result;
    result.reserve(zoneCount.size());

    // Dump the map data into a list, flat structure so we can sort it.
    for (const auto &it : zoneCount)
        result.push_back({it.first, it.second});

    // For Tie breakers
    // 1The higher count wins 2If counts are equal, the lexicographically smaller zone get priority to come first.
    auto cmp = [](const ZoneCount &a, const ZoneCount &b)
    {
        if (a.count != b.count)
            return a.count > b.count;
        return a.zone < b.zone;
    };

    if ((int)result.size() > k)
    {
        // Here we only nned the top K so partial_sort is more efficient if we sorted the whole list.
        partial_sort(result.begin(), result.begin() + k, result.end(), cmp);
        result.resize(k);
    }
    else
    {
        sort(result.begin(), result.end(), cmp);
    }
    return result;
}

// K is being returned busiest time slots.
std::vector<SlotCount> TripAnalyzer::topBusySlots(int k) const
{
    vector<SlotCount> result;
    result.reserve(slotCount.size());

    for (const auto &it : slotCount)
    {
        result.push_back({it.first.first, it.first.second, it.second});
    }

    // this is a tie breaker for sloting first, then Zone Name, then Hour.
    auto cmp = [](const SlotCount &a, const SlotCount &b)
    {
        if (a.count != b.count)
            return a.count > b.count;
        if (a.zone != b.zone)
            return a.zone < b.zone;
        return a.hour < b.hour;
    };

    if ((int)result.size() > k)
    {
        // Again here we use partial_sort to save time.
        partial_sort(result.begin(), result.begin() + k, result.end(), cmp);
        result.resize(k);
    }
    else
    {
        sort(result.begin(), result.end(), cmp);
    }
    return result;
}
