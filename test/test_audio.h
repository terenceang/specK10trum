#pragma once

class AudioTest {
public:
    // Run all Beeper validation cases. Returns true if all passed.
    static bool runAllTests();

private:
    static bool testSilenceFrame();
    static bool testSpeakerOnlyTransition();
    static bool testTapeOnlyTransition();
    static bool testSimultaneousTransition();
};
