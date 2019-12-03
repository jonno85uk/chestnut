#ifndef CLIPTEST_H
#define CLIPTEST_H

#include <QtTest>
#include <QObject>

class ClipTest : public QObject
{
    Q_OBJECT
  public:
     ClipTest();

  signals:

  private slots:
     void testCaseConstructor();
     /**
      * @brief Test caching is not used with an empty, unused clip
      */
     void testCaseUsesCacherDefault();
     /**
      * @brief Test caching used with a Media item assigned of type footage only
      */
     void testCaseUsesCacher();

     /**
      * @brief Test that the Clip's media is open
      */
     void testCaseMediaOpen();

     void testCaseClipType();

     void testCaseisSelected();

     void testCaseaddLinkedClip();

     void testCaseRelink();

     void testCaseClearLinks();

     void testCaseSetTransition();

     void testCaseSplit();

     void testCaseSplitWithClosingTransition();

     void testCaseSplitWithOpeningTransition();

     void testCaseSplitWithBothTransitions();

     void testCaseDeleteTransition();

     void testCaseNudge();

     void testCaseInRange();

     void testCaseIsSelectedBySelection();

     void testCaseIsSelected();
     /**
      * @brief test when 2 clips are side by side with a shared transition
      */
     void testCaseVerifyTransitionSideBySide();
     /**
      * @brief test when 2 clips that were side by side with a shared transition and have now moved apart along the timeline
      */
     void testCaseVerifyTransitionMovedTime();
     /**
      * @brief test when 2 clips that were side by side with a shared transition and have now moved tracks
      */
     void testCaseVerifyTransitionMovedTrack();
     /**
      * @brief Test returned timestamp when playhead is in clips range
      */
     void testCasePlayheadToTimestampInRange();

     void testCasePlayheadToTimestampBeforeRange();
     void testCasePlayheadToTimestampAfterRange();
};

#endif // CLIPTEST_H
