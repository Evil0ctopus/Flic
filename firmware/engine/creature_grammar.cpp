#include "creature_grammar.h"

#include <vector>

namespace Flic {
namespace {

bool isArticle(const String& token) {
    return token == "the" || token == "a" || token == "an";
}

bool isSpikeEmotion(const String& emotion) {
    return emotion == "mischievous" || emotion == "curious" || emotion == "surprised" || emotion == "excited";
}

}  // namespace

String buildCreatureSpeech(const char* input, const String& emotionState, float chaosLevel) {
    (void)chaosLevel;

    if (input == nullptr) {
        return String();
    }

    String cleaned = String(input);
    cleaned.replace("\n", " ");
    cleaned.replace("\r", " ");
    cleaned.replace("?", "");
    cleaned.replace("!", "");
    cleaned.replace(",", " ");
    cleaned.replace(".", " ");
    cleaned.trim();

    if (cleaned.length() == 0) {
        return "hmm";
    }

    std::vector<String> words;
    int start = 0;
    while (start < cleaned.length()) {
        while (start < cleaned.length() && cleaned.charAt(start) == ' ') {
            ++start;
        }
        if (start >= cleaned.length()) {
            break;
        }
        int end = start;
        while (end < cleaned.length() && cleaned.charAt(end) != ' ') {
            ++end;
        }
        String token = cleaned.substring(start, end);
        String lowered = token;
        lowered.toLowerCase();
        if (!isArticle(lowered)) {
            words.push_back(token);
        }
        start = end + 1;
    }

    if (words.empty()) {
        words.push_back("gremlin");
    }

    size_t maxWords = 7;
    if (emotionState == "sleepy") {
        maxWords = 4;
    } else if (isSpikeEmotion(emotionState)) {
        maxWords = 9;
    }
    if (words.size() > maxWords) {
        words.resize(maxWords);
    }

    if (isSpikeEmotion(emotionState) && words.size() >= 3) {
        const String first = words[0];
        words.erase(words.begin());
        words.push_back(first);
    }

    String output;
    const char* noise = (emotionState == "surprised") ? "eep" : ((emotionState == "mischievous") ? "hah" : "hmm");
    output += noise;
    output += " ";

    for (size_t i = 0; i < words.size(); ++i) {
        output += words[i];
        if (i + 1 < words.size()) {
            output += " ";
        }
    }

    if (emotionState == "mischievous" || emotionState == "excited") {
        output += " hehe tiny chaos";
    } else if (emotionState == "curious") {
        output += " mm maybe?";
    } else if (emotionState == "sleepy") {
        output += " zzz tiny";
    } else {
        output += " okay friend";
    }

    return output;
}

}  // namespace Flic
