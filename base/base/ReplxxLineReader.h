#pragma once

#include "LineReader.h"

#include <replxx.hxx>

enum FuzzyFinderType
{
    FUZZY_FINDER_NONE,
    /// Use https://github.com/junegunn/fzf
    FUZZY_FINDER_FZF,
    /// Use https://github.com/lotabout/skim
    FUZZY_FINDER_SKIM,
};

class ReplxxLineReader : public LineReader
{
public:
    ReplxxLineReader(
        Suggest & suggest,
        const String & history_file_path,
        bool multiline,
        Patterns extenders_,
        Patterns delimiters_,
        replxx::Replxx::highlighter_callback_t highlighter_);
    ~ReplxxLineReader() override;

    void enableBracketedPaste() override;

    /// If highlight is on, we will set a flag to denote whether the last token is a delimiter.
    /// This is useful to determine the behavior of <ENTER> key when multiline is enabled.
    static void setLastIsDelimiter(bool flag);
private:
    InputStatus readOneLine(const String & prompt) override;
    void addToHistory(const String & line) override;
    int executeEditor(const std::string & path);
    void openEditor();
    void openInteractiveHistorySearch();

    replxx::Replxx rx;
    replxx::Replxx::highlighter_callback_t highlighter;

    // used to call flock() to synchronize multiple clients using same history file
    int history_file_fd = -1;
    bool bracketed_paste_enabled = false;

    std::string editor;
    std::string fuzzy_finder;
    FuzzyFinderType fuzzy_finder_type = FUZZY_FINDER_NONE;
};
