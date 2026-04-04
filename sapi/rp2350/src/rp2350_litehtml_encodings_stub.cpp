#include "litehtml/encodings.h"

namespace litehtml {

encoding bom_sniff(const string&) {
    return encoding::utf_8;
}

void encoding_sniffing_algorithm(estring& str) {
    str.encoding = encoding::utf_8;
    str.confidence = confidence::certain;
}

encoding get_encoding(string) {
    return encoding::utf_8;
}

encoding extract_encoding_from_meta_element(string) {
    return encoding::utf_8;
}

void decode(string input, encoding, string& output) {
    output = std::move(input);
}

string decode(string input, encoding) {
    return input;
}

}
