#include "cliptest.h"
#include "project/clip.h"
#include "project/transition.h"
#include "project/undo.h"


ClipTest::ClipTest()
{

}


void ClipTest::testCaseConstructor()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);
  QVERIFY(clp.sequence == seq);
  QVERIFY(clp.closingTransition() == nullptr);
  QVERIFY(clp.openingTransition() == nullptr);
  QVERIFY(clp.height() == -1);
  QVERIFY(clp.width() == -1);
  QVERIFY(clp.maximumLength() == -1);
  QVERIFY(clp.isActive(0) == false);
  QVERIFY(clp.isSelected(false) == false);
}


void ClipTest::testCaseUsesCacherDefault()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);
  QVERIFY(clp.usesCacher() == false);
}


void ClipTest::testCaseUsesCacher()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);
  auto mda =  std::make_shared<Media>();
  mda->setFootage(std::make_shared<Footage>(mda));
  clp.timeline_info.media = mda;
  QVERIFY(clp.usesCacher() == true);
  mda = std::make_shared<Media>();
  mda->setSequence(std::make_shared<Sequence>());
  clp.timeline_info.media = mda;
  QVERIFY(clp.usesCacher() == false);
}


void ClipTest::testCaseMediaOpen()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);
  QVERIFY(clp.mediaOpen() == false);
  auto mda =  std::make_shared<Media>();
  mda->setFootage(std::make_shared<Footage>(mda));
  clp.timeline_info.media = mda;
  QVERIFY(clp.mediaOpen() == false);
  clp.finished_opening = true;
  QVERIFY(clp.mediaOpen() == true);
  mda->clearObject();
  QVERIFY(clp.mediaOpen() == false);
  mda->setSequence(std::make_shared<Sequence>());
  QVERIFY(clp.mediaOpen() == false);
}


void ClipTest::testCaseClipType()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);
  clp.timeline_info.track_ = 100;
  QVERIFY(clp.mediaType() == ClipType::AUDIO);
  clp.timeline_info.track_ = -100;
  QVERIFY(clp.mediaType() == ClipType::VISUAL);
  clp.timeline_info.track_ = 0;
  QVERIFY(clp.mediaType() == ClipType::AUDIO);
}

void ClipTest::testCaseisSelected()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);
  Selection sel;
  sel.track = 1;
  clp.timeline_info.track_ = 0;
  QVERIFY(clp.isSelected(sel) == false);
  clp.timeline_info.in = 0;
  clp.timeline_info.out = 1;
  clp.timeline_info.track_ = 1;
  QVERIFY(clp.isSelected(sel) == false);
  sel.in = 0;
  sel.out = 100;
  QVERIFY(clp.isSelected(sel) == true);
  sel.in = 1;
  QVERIFY(clp.isSelected(sel) == false);
  clp.timeline_info.in = 100;
  clp.timeline_info.out = 101;
  QVERIFY(clp.isSelected(sel) == false);
}


void ClipTest::testCaseaddLinkedClip()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);

  Clip linked_clip(seq);
  clp.addLinkedClip(linked_clip);

  QVERIFY(clp.linked.contains(linked_clip.id_) == true);
}

void ClipTest::testCaseRelink()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);

  Clip linked_clip(seq);
  clp.addLinkedClip(linked_clip);

  auto copied_link = linked_clip.copy(seq);
  QMap<int, int> mapped_links;
  mapped_links[linked_clip.id_] = copied_link->id();

  clp.relink(mapped_links);

  QVERIFY(clp.linked.contains(copied_link->id_));
  QVERIFY(clp.linked.size() == 1);
}


void ClipTest::testCaseClearLinks()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);
  clp.linked.append(1);
  clp.linked.append(2);
  clp.linked.append(3);
  clp.clearLinks();
  QVERIFY(clp.linkedClipIds().empty());
}


void ClipTest::testCaseSetTransition()
{
  auto seq = std::make_shared<Sequence>();
  auto clp = std::make_shared<Clip>(seq);
  EffectMeta meta;
  meta.type = TRANSITION_INTERNAL_CROSSDISSOLVE;
  meta.internal = 0;
  clp->timeline_info.in = 0;
  clp->timeline_info.out = 0;
  // Attempt to set transition for clip of length zero
  QVERIFY(clp->setTransition(meta, ClipTransitionType::CLOSING, 1000) == false);
  clp->timeline_info.in = 0;
  clp->timeline_info.out = 500;
  // Attempt to set transition of length greater than clip length
  QVERIFY(clp->setTransition(meta, ClipTransitionType::CLOSING, 1000) == true);
  QVERIFY(clp->getTransition(ClipTransitionType::CLOSING) != nullptr);
  QVERIFY(clp->getTransition(ClipTransitionType::CLOSING)->get_length() == clp->length());
}


void ClipTest::testCaseSplit()
{
  auto seq = std::make_shared<Sequence>();
  auto clp = std::make_shared<Clip>(seq);
  clp->timeline_info.in = 0;
  clp->timeline_info.clip_in = 0;
  clp->timeline_info.out = 1000;

  auto split_clip = clp->split(500);

  QVERIFY(split_clip != nullptr);
  QVERIFY(split_clip->length() == 500);
  QVERIFY(split_clip->timeline_info.in == 500);
  QVERIFY(split_clip->timeline_info.clip_in == 500);
  QVERIFY(clp->length() == 500);
  QVERIFY(clp->timeline_info.in == 0);
  QVERIFY(clp->timeline_info.clip_in == 0);
  QVERIFY(clp->timeline_info.out == 500);
}

void ClipTest::testCaseSplitWithClosingTransition()
{
  auto seq = std::make_shared<Sequence>();
  auto clp = std::make_shared<Clip>(seq);
  EffectMeta meta;
  meta.type = TRANSITION_INTERNAL_CROSSDISSOLVE;
  meta.internal = 0;
  clp->timeline_info.in = 0;
  clp->timeline_info.clip_in = 0;
  clp->timeline_info.out = 1000;
  clp->setTransition(meta, ClipTransitionType::CLOSING, 500);
  auto orig_tran = clp->getTransition(ClipTransitionType::CLOSING);
  // split clip into the closing transition
  auto split_clip = clp->split(900);
  QVERIFY(clp->getTransition(ClipTransitionType::CLOSING) == nullptr);
  // ensure ownership has been transferred
  QVERIFY(split_clip->getTransition(ClipTransitionType::CLOSING) == orig_tran);
  // transition in split should be resized to fit
  QVERIFY(split_clip->getTransition(ClipTransitionType::CLOSING)->get_length() == 100);
  //clip should be of same length as transition
  QVERIFY(split_clip->length() == 100);
}


void ClipTest::testCaseSplitWithOpeningTransition()
{
  auto seq = std::make_shared<Sequence>();
  auto clp = std::make_shared<Clip>(seq);
  EffectMeta meta;
  meta.type = TRANSITION_INTERNAL_CROSSDISSOLVE;
  meta.internal = 0;
  clp->timeline_info.in = 0;
  clp->timeline_info.clip_in = 0;
  clp->timeline_info.out = 1000;
  clp->setTransition(meta, ClipTransitionType::OPENING, 500);
  // split clip into the opening transition
  auto split_clip = clp->split(100);
  // original clip should keep the transition but length modified
  QVERIFY(clp->getTransition(ClipTransitionType::OPENING) != nullptr);
  QVERIFY(split_clip->getTransition(ClipTransitionType::OPENING) == nullptr);
  // transition in original should be resized to fit
  QVERIFY(clp->getTransition(ClipTransitionType::OPENING)->get_length() == 100);
  //original clip should be of same length as transition
  QVERIFY(clp->length() == 100);
}


void ClipTest::testCaseSplitWithBothTransitions()
{
  auto seq = std::make_shared<Sequence>();
  auto clp = std::make_shared<Clip>(seq);
  EffectMeta meta;
  meta.type = TRANSITION_INTERNAL_CROSSDISSOLVE;
  meta.internal = 0;
  clp->timeline_info.in = 0;
  clp->timeline_info.clip_in = 0;
  clp->timeline_info.out = 1000;
  auto trans_len = 100;
  clp->setTransition(meta, ClipTransitionType::OPENING, trans_len);
  clp->setTransition(meta, ClipTransitionType::CLOSING, trans_len);
  // split clip between transitions
  auto split_clip = clp->split(500);
  // original clip should keep the opening transition, unmodified
  QVERIFY(clp->getTransition(ClipTransitionType::OPENING) != nullptr);
  QVERIFY(clp->getTransition(ClipTransitionType::OPENING)->get_length() == trans_len);
  QVERIFY(clp->getTransition(ClipTransitionType::CLOSING) == nullptr);
  // split clip should get the closing transition, unmodified
  QVERIFY(split_clip->getTransition(ClipTransitionType::OPENING) == nullptr);
  QVERIFY(split_clip->getTransition(ClipTransitionType::CLOSING) != nullptr);
  QVERIFY(split_clip->getTransition(ClipTransitionType::CLOSING)->get_length() == trans_len);
}


void ClipTest::testCaseDeleteTransition()
{
  auto seq = std::make_shared<Sequence>();
  auto clp = std::make_shared<Clip>(seq);
  clp->timeline_info.in = 0;
  clp->timeline_info.clip_in = 0;
  clp->timeline_info.out = 1000;

  EffectMeta meta;
  meta.type = TRANSITION_INTERNAL_CROSSDISSOLVE;
  meta.internal = 0;
  auto trans_len = 100;
  clp->setTransition(meta, ClipTransitionType::OPENING, trans_len);
  clp->setTransition(meta, ClipTransitionType::CLOSING, trans_len);

  // ensure transition is null on delete
  clp->deleteTransition(ClipTransitionType::OPENING);
  QVERIFY(clp->getTransition(ClipTransitionType::OPENING) == nullptr);
  clp->deleteTransition(ClipTransitionType::CLOSING);
  QVERIFY(clp->getTransition(ClipTransitionType::CLOSING) == nullptr);

  // ensure all transitions are null on delete
  clp->setTransition(meta, ClipTransitionType::OPENING, trans_len);
  clp->setTransition(meta, ClipTransitionType::CLOSING, trans_len);
  clp->deleteTransition(ClipTransitionType::BOTH);
  QVERIFY(clp->getTransition(ClipTransitionType::OPENING) == nullptr);
  QVERIFY(clp->getTransition(ClipTransitionType::CLOSING) == nullptr);

}


void ClipTest::testCaseNudge()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);
  clp.timeline_info.in = 0;
  clp.timeline_info.clip_in = 0;
  clp.timeline_info.out = 1000;

  // advance clip in timeline by 1 frame
  clp.nudge(1);
  QVERIFY(clp.timeline_info.in == 1);
  QVERIFY(clp.timeline_info.out == 1001);
}


void ClipTest::testCaseInRange()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);
  clp.timeline_info.in = 0;
  clp.timeline_info.clip_in = 0;
  clp.timeline_info.out = 1000;

  QVERIFY(clp.inRange(500) == true);
  QVERIFY(clp.inRange(-500) == false);
  QVERIFY(clp.inRange(0) == false);
  QVERIFY(clp.inRange(1000) == false);
  QVERIFY(clp.inRange(10000) == false);
}


void ClipTest::testCaseIsSelectedBySelection()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);
  clp.timeline_info.in = 0;
  clp.timeline_info.clip_in = 0;
  clp.timeline_info.out = 1000;
  clp.timeline_info.track_ = 0;
  clp.timeline_info.enabled = true;

  Selection sel;
  sel.in = 100;
  sel.out = 200;
  sel.track = 0;
  // Selection is a transition
  sel.transition_ = true;
  QVERIFY(clp.isSelected(sel) == false);
  // Selection is not a transition
  sel.transition_ = false;
  QVERIFY(clp.isSelected(sel) == true);
  //Selection is on different track
  sel.track = 100;
  QVERIFY(clp.isSelected(sel) == false);
  // selection is greater than clip
  sel.track = 0;
  sel.in = 0;
  sel.out = 10000;
  QVERIFY(clp.isSelected(sel) == true);
  // selecting an unenabled clip //TODO: what should be the behaviour???
  clp.timeline_info.enabled = false;
  QVERIFY(clp.isSelected(sel) == true);
}


void ClipTest::testCaseIsSelected()
{
  auto seq = std::make_shared<Sequence>();
  Clip clp(seq);
  clp.timeline_info.in = 0;
  clp.timeline_info.clip_in = 0;
  clp.timeline_info.out = 1000;
  clp.timeline_info.track_ = 0;
  clp.timeline_info.enabled = true;

  Selection sel;
  sel.in = 0;
  sel.out = 1000;
  sel.track = 0;
  // Selection is a transition
  sel.transition_ = true;
  seq->selections_.append(sel);
  // is contained in a selection
  QVERIFY(clp.isSelected(true) == false);
  // is not contained in a selection
  QVERIFY(clp.isSelected(false) == true);

  seq->selections_.clear();
  // Selection is a clip
  sel.transition_ = false;
  seq->selections_.append(sel);
  // is contained in a selection
  QVERIFY(clp.isSelected(true) == true);
  // is not contained in a selection
  QVERIFY(clp.isSelected(false) == false);

}


void ClipTest::testCaseVerifyTransitionSideBySide()
{
  ComboAction ca;

  auto seq = std::make_shared<Sequence>();
  auto clp = std::make_shared<Clip>(seq);
  auto sec_clp = std::make_shared<Clip>(seq);
  EffectMeta meta;
  meta.type = TRANSITION_INTERNAL_CROSSDISSOLVE;
  meta.internal = 0;
  auto out_tran = std::make_shared<Transition>(clp, sec_clp, meta);
  auto in_tran = std::make_shared<Transition>(sec_clp, clp, meta);

  clp->timeline_info.out = 100;
  clp->timeline_info.track_ = 1;
  clp->transition_.closing_ = out_tran;
  sec_clp->timeline_info.in = 100;
  sec_clp->timeline_info.track_ = 1;
  sec_clp->transition_.opening_ = in_tran;

  clp->verifyTransitions(ca);
  sec_clp->verifyTransitions(ca);
  QVERIFY(ca.size() == 0);
}

void ClipTest::testCaseVerifyTransitionMovedTime()
{
  ComboAction ca;

  auto seq = std::make_shared<Sequence>();
  auto clp = std::make_shared<Clip>(seq);
  auto sec_clp = std::make_shared<Clip>(seq);
  EffectMeta meta;
  meta.type = TRANSITION_INTERNAL_CROSSDISSOLVE;
  meta.internal = 0;
  auto out_tran = std::make_shared<Transition>(clp, sec_clp, meta);
  auto in_tran = std::make_shared<Transition>(sec_clp, clp, meta);

  clp->timeline_info.out = 100;
  clp->timeline_info.track_ = 1;
  clp->transition_.closing_ = out_tran;
  sec_clp->timeline_info.in = 101;
  sec_clp->timeline_info.track_ = 1;
  sec_clp->transition_.opening_ = in_tran;

  clp->verifyTransitions(ca);
  QVERIFY(ca.size() == 2);
  sec_clp->verifyTransitions(ca);
  QVERIFY(ca.size() == 4);
}


void ClipTest::testCaseVerifyTransitionMovedTrack()
{
  ComboAction ca;

  auto seq = std::make_shared<Sequence>();
  auto clp = std::make_shared<Clip>(seq);
  auto sec_clp = std::make_shared<Clip>(seq);
  EffectMeta meta;
  meta.type = TRANSITION_INTERNAL_CROSSDISSOLVE;
  meta.internal = 0;
  auto out_tran = std::make_shared<Transition>(clp, sec_clp, meta);
  auto in_tran = std::make_shared<Transition>(sec_clp, clp, meta);

  clp->timeline_info.out = 100;
  clp->timeline_info.track_ = 1;
  clp->transition_.closing_ = out_tran;
  sec_clp->timeline_info.in = 100;
  sec_clp->timeline_info.track_ = 0;
  sec_clp->transition_.opening_ = in_tran;

  clp->verifyTransitions(ca);
  QVERIFY(ca.size() == 2);
  sec_clp->verifyTransitions(ca);
  QVERIFY(ca.size() == 4);
}

class MediaTestable : public Media
{
  public:
    MediaTestable()
    {
      object_ = std::make_shared<Footage>(nullptr);
    }
    MediaType type() const noexcept override
    {
      return MediaType::FOOTAGE;
    }
    void setSpeed(double speed)
    {
      std::dynamic_pointer_cast<Footage>(object_)->speed_ = speed;
    }
};

void ClipTest::testCasePlayheadToTimestampInRange()
{
  auto seq = std::make_shared<Sequence>();
  seq->setFrameRate(10);
  auto mda = std::make_shared<MediaTestable>();
  mda->setSpeed(1.0);
  auto clp = std::make_shared<Clip>(seq);
  clp->timeline_info.clip_in = 0;
  clp->timeline_info.in = 10;
  clp->timeline_info.out = 20;
  clp->timeline_info.media = mda;
  auto val = clp->playhead_to_seconds(15);
  QCOMPARE(val, 0.5);
  clp->timeline_info.clip_in = 5;
  val = clp->playhead_to_seconds(15);
  QCOMPARE(val, 1.0);
  mda->setSpeed(0.1);
  clp->timeline_info.clip_in = 0;
  val = clp->playhead_to_seconds(15);
  QCOMPARE(val, 0.05);
}


void ClipTest::testCasePlayheadToTimestampBeforeRange()
{
  auto seq = std::make_shared<Sequence>();
  seq->setFrameRate(10);
  auto mda = std::make_shared<MediaTestable>();
  mda->setSpeed(1.0);
  auto clp = std::make_shared<Clip>(seq);
  clp->timeline_info.clip_in = 0;
  clp->timeline_info.in = 10;
  clp->timeline_info.out = 20;
  clp->timeline_info.media = mda;
  auto val = clp->playhead_to_seconds(5);
  QCOMPARE(val, 0);
}


void ClipTest::testCasePlayheadToTimestampAfterRange()
{
  auto seq = std::make_shared<Sequence>();
  seq->setFrameRate(10);
  auto mda = std::make_shared<MediaTestable>();
  mda->setSpeed(1.0);
  auto clp = std::make_shared<Clip>(seq);
  clp->timeline_info.clip_in = 0;
  clp->timeline_info.in = 10;
  clp->timeline_info.out = 20;
  clp->timeline_info.media = mda;
  auto val = clp->playhead_to_seconds(25);
  QCOMPARE(val, 1.5);
}
