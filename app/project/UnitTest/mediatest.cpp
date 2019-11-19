#include "mediatest.h"

#include "project/media.h"

MediaTest::MediaTest()
{

}


void MediaTest::testCaseConstructor()
{
  auto current = Media::nextID; //post incremented in Media
  Media mda;
  QVERIFY(mda.id() == current);
  QVERIFY(mda.object<Footage>() == nullptr);
  QVERIFY(mda.object<Sequence>() == nullptr);
  QVERIFY(mda.type() == MediaType::NONE);
  QVERIFY(mda.name().isEmpty());
  QVERIFY(mda.childCount() == 0);
  QVERIFY(mda.child(0) == nullptr);
  QVERIFY(mda.columnCount() == 3);
  QVERIFY(mda.data(0,0).isValid());
  QVERIFY(mda.row() == 0);
  QVERIFY(mda.parentItem() == nullptr);
  QVERIFY(mda.root_ == true);
}


void MediaTest::testCaseConstructorWithParent()
{
  auto parent = std::make_shared<Media>();
  Media child(parent);
  QVERIFY(child.parentItem() != nullptr);
  QVERIFY(child.parentItem() == parent);
  QVERIFY(child.root_ == false);
}


void MediaTest::testCaseSetAsFootage()
{
  Media mda;
  auto ftg = std::make_shared<Footage>(nullptr);
  QVERIFY(mda.setFootage(ftg) == true);
  QVERIFY(mda.setFootage(ftg) == false);
  ftg.reset();
  QVERIFY(mda.setFootage(ftg) == false);
  QVERIFY(mda.type() == MediaType::FOOTAGE);
  QVERIFY(mda.object<Footage>() != nullptr);
  QVERIFY(mda.root_ == false);
}

void MediaTest::testCaseSetAsSequence()
{
  auto mda = std::make_shared<Media>();
  auto sqn = std::make_shared<Sequence>();
  QVERIFY(mda->setSequence(sqn) == true);
  QVERIFY(mda->setSequence(sqn) == false);
  sqn.reset();
  QVERIFY(mda->setSequence(sqn) == false);
  QVERIFY(mda->type() == MediaType::SEQUENCE);
  QVERIFY(mda->object<Sequence>() != nullptr);
  QVERIFY(mda->root_ == false);
}


void MediaTest::testCaseSetAsFolder()
{
  Media mda;
  mda.setFolder();
  QVERIFY(mda.type() == MediaType::FOLDER);
  QVERIFY(!mda.name().isEmpty());
  QVERIFY(mda.root_ == false);
}

void MediaTest::testCaseClear()
{
  Media mda;
  auto ftg = std::make_shared<Footage>(nullptr);
  mda.setFootage(ftg);
  mda.clearObject();
  QVERIFY(mda.type() == MediaType::NONE);
  QVERIFY(mda.object<Footage>() == nullptr);
  QVERIFY(mda.root_ == true);
}

/**
 * @brief Check that when its id is set Media::nextID is greater
 */
void MediaTest::testCaseSetId()
{
  int id_begin = Media::nextID;
  Media mda;
  QVERIFY(id_begin == mda.id());
  QVERIFY(Media::nextID > id_begin);
  int new_id = Media::nextID + 100; // 100 has no significance
  mda.setId(new_id);
  QVERIFY(Media::nextID > mda.id());

}
