#pragma once
#include "utility/Keyboard/KeyboardReader/KeyboardReader.h"

/* TCA8418 reader that empties the event FIFO on every update.
 * The stock reader takes one event per call and drops the second key
 * of an Fn+arrow chord, so volume never sees both keys down. */
class PoseidonTcaReader : public KeyboardReader {
public:
    void begin() override;
    void update() override;
};
