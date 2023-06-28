#include <algorithm>
#include <stdexcept>
#include <array>
#include <string>
#include <string_view>

#include "base64.h"

//
// Depending on the url parameter in base64_chars, one of
// two sets of base64 characters needs to be chosen.
// They differ in their last two characters.
//
static constexpr std::string_view encode_table = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "+/" };

static constexpr std::string_view encode_table_url = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "-_" };

static constexpr size_t max_encode_char = static_cast<size_t>('z') + 1;
static constexpr size_t decode_placeholder = 0xff;

static constexpr std::array<size_t, max_encode_char> generate_decode_table(std::string_view str)
{
    std::array<size_t, max_encode_char> ret{};
#if __clang_major__ < 15
    for (int i = 0; i < ret.size(); ++i)
        ret[i] = decode_placeholder;
#else
    ret.fill(decode_placeholder);
#endif
    std::for_each(str.begin(), str.end(), [&ret, offset = 0](const char& ch) mutable constexpr
        { ret.at(static_cast<size_t>(ch)) = offset++; });
    return ret;
}

static constexpr std::array<size_t, max_encode_char> decode_table = generate_decode_table(encode_table);

static size_t pos_of_char(const char chr)
{
    //
    // Return the position of chr within base64_encode()
    //
    if (static_cast<size_t>(chr) >= max_encode_char)
        throw std::runtime_error("Input is not valid base64-encoded data.");
    else if (auto val = decode_table.at(static_cast<size_t>(chr)); val == decode_placeholder)
        throw std::runtime_error("Input is not valid base64-encoded data.");
    else
        return val;
}

static std::string insert_linebreaks(std::string str, size_t distance)
{
    //
    // Provided by https://github.com/JomaCorpFX, adapted by me.
    //
    if (!str.length())
    {
        return "";
    }

    size_t pos = distance;

    while (pos < str.size())
    {
        str.insert(pos, "\n");
        pos += distance + 1;
    }

    return str;
}

//
// Interface with std::string_view rather than const std::string&
// Requires C++17
// Provided by Yannic Bonenberger (https://github.com/Yannic)
//

std::string base64_encode(std::string_view s, bool url)
{
    auto bytes_to_encode = reinterpret_cast<const unsigned char*>(s.data());
    size_t in_len = s.length();
    size_t len_encoded = (in_len + 2) / 3 * 4;

    unsigned char trailing_char = url ? '.' : '=';

    //
    // Choose set of base64 characters. They differ
    // for the last two positions, depending on the url
    // parameter.
    // A bool (as is the parameter url) is guaranteed
    // to evaluate to either 0 or 1 in C++ therefore,
    // the correct character set is chosen by subscripting
    // base64_chars with url.
    //
    auto base64_chars_ = url ? encode_table_url : encode_table;

    std::string ret;
    ret.reserve(len_encoded);

    unsigned int pos = 0;

    while (pos < in_len)
    {
        ret.push_back(base64_chars_[(bytes_to_encode[pos + 0] & 0xfc) >> 2]);

        if (pos + 1 < in_len)
        {
            ret.push_back(base64_chars_[((bytes_to_encode[pos + 0] & 0x03) << 4) + ((bytes_to_encode[pos + 1] & 0xf0) >> 4)]);

            if (pos + 2 < in_len)
            {
                ret.push_back(base64_chars_[((bytes_to_encode[pos + 1] & 0x0f) << 2) + ((bytes_to_encode[pos + 2] & 0xc0) >> 6)]);
                ret.push_back(base64_chars_[bytes_to_encode[pos + 2] & 0x3f]);
            }
            else
            {
                ret.push_back(base64_chars_[(bytes_to_encode[pos + 1] & 0x0f) << 2]);
                ret.push_back(trailing_char);
            }
        }
        else
        {

            ret.push_back(base64_chars_[(bytes_to_encode[pos + 0] & 0x03) << 4]);
            ret.push_back(trailing_char);
            ret.push_back(trailing_char);
        }

        pos += 3;
    }

    return ret;
}

std::string base64_encode_pem(std::string_view s)
{
    return insert_linebreaks(base64_encode(s, false), 64);
}

std::string base64_encode_mime(std::string_view s)
{
    return insert_linebreaks(base64_encode(s, false), 76);
}

std::string base64_decode(std::string_view encoded_string, bool remove_linebreaks)
{
    //
    // decode(��) is templated so that it can be used with String = const std::string&
    // or std::string_view (requires at least C++17)
    //

    if (encoded_string.empty())
        return std::string();

    if (remove_linebreaks)
    {

        std::string copy(encoded_string);

        copy.erase(std::remove(copy.begin(), copy.end(), '\n'), copy.end());

        return base64_decode(copy, false);
    }

    size_t length_of_string = encoded_string.length();
    size_t pos = 0;

    //
    // The approximate length (bytes) of the decoded string might be one or
    // two bytes smaller, depending on the amount of trailing equal signs
    // in the encoded string. This approximation is needed to reserve
    // enough space in the string to be returned.
    //
    size_t approx_length_of_decoded_string = length_of_string / 4 * 3;
    std::string ret;
    ret.reserve(approx_length_of_decoded_string);

    while (pos < length_of_string)
    {
        //
        // Iterate over encoded input string in chunks. The size of all
        // chunks except the last one is 4 bytes.
        //
        // The last chunk might be padded with equal signs or dots
        // in order to make it 4 bytes in size as well, but this
        // is not required as per RFC 2045.
        //
        // All chunks except the last one produce three output bytes.
        //
        // The last chunk produces at least one and up to three bytes.
        //

        size_t pos_of_char_1 = pos_of_char(encoded_string.at(pos + 1));

        //
        // Emit the first output byte that is produced in each chunk:
        //
        ret.push_back(static_cast<std::string::value_type>(((pos_of_char(encoded_string.at(pos + 0))) << 2) + ((pos_of_char_1 & 0x30) >> 4)));

        if ((pos + 2 < length_of_string) && // Check for data that is not padded with equal signs (which is allowed by RFC 2045)
            encoded_string.at(pos + 2) != '=' &&
            encoded_string.at(pos + 2) != '.' // accept URL-safe base 64 strings, too, so check for '.' also.
            )
        {
            //
            // Emit a chunk's second byte (which might not be produced in the last chunk).
            //
            size_t pos_of_char_2 = pos_of_char(encoded_string.at(pos + 2));
            ret.push_back(static_cast<std::string::value_type>(((pos_of_char_1 & 0x0f) << 4) + ((pos_of_char_2 & 0x3c) >> 2)));

            if ((pos + 3 < length_of_string) &&
                encoded_string.at(pos + 3) != '=' &&
                encoded_string.at(pos + 3) != '.')
            {
                //
                // Emit a chunk's third byte (which might not be produced in the last chunk).
                //
                ret.push_back(static_cast<std::string::value_type>(((pos_of_char_2 & 0x03) << 6) + pos_of_char(encoded_string.at(pos + 3))));
            }
        }

        pos += 4;
    }

    return ret;
}
