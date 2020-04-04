#ifndef SEQUENCETEST_H
#define SEQUENCETEST_H

#include <QtTest>

#include "project/sequence.h"

namespace chestnut::project
{

class SequenceTest : public QObject
{
    Q_OBJECT
public:
    SequenceTest();
private slots:
    void testCaseDefaults();
    void testCaseCopy();
    void testCaseSetWidths_data();
    void testCaseSetWidths();
    void testCaseSetHeights_data();
    void testCaseSetHeights();
    void testCaseSetFrameRate_data();
    void testCaseSetFrameRate();
    void testCaseSetFrequency_data();
    void testCaseSetFrequency();
    /**
     * @brief Check correct tracks are returned from a populated sequence
     */
    void testCaseTracks();
    /**
     * @brief Check correct clips are returned at a timeline position
     */
    void testCaseClips();

    void testCaseTrackCount();

    void testCaseTrackCountAudioVidClip();

    void testCaseTrackLocked();

    void testCaseTrackEnabled();

    void testCaseLockTrack();

    void testCaseEnableTrack();

    void testCaseAddClip();

    void testCaseAddClipDuplicate();

    void testCaseAddClipNull();

    void testCaseVerifyTracksEmpty();

    void testCaseVerifyTracksSingleAudio();

    void testCaseVerifyTracksSingleVideo();

    void testCaseVerifyTracksSingleVideoAudio();

    void testCaseVerifyTracksSingleVideoAudioRemoved();

    void testCaseVerifyTracksMoveAudioUp();

    void testCaseVerifyTracksMoveAudioDown();

    void testCaseVerifyTracksRemoveAudio();

    void testCaseAddSelectionLocked();

    void testCaseAddSelectionNotLocked();

    void testCaseEndFrameEmptySequence();

    void testCaseEndFramePopulatedSequence();

    void testCaseActiveLengthEmptySequence();

    void testCaseActiveLengthPopulatedSequence();

    void testCaseActiveLengthPopulatedSequenceInOutSet();

};
}

#endif // SEQUENCETEST_H
