#include "projectmodeltest.h"
#include "project/projectmodel.h"

#include <QtTest>

using chestnut::project::Media;
using chestnut::project::Sequence;

ProjectModelTest::ProjectModelTest(QObject *parent) : QObject(parent)
{

}


void ProjectModelTest::testCaseConstructor()
{
  ProjectModel model;
  QVERIFY(model.project_items.size() == 1); //root item
}


void ProjectModelTest::testCaseInsert()
{
  ProjectModel model;
  auto item = std::make_shared<Media>();
  auto count = model.project_items.size();
  model.insert(item);
  QVERIFY(model.project_items.size() == count + 1);
}

void ProjectModelTest::testCaseAppendChildWithSequence()
{
  ProjectModel model;
  auto count = model.project_items.size();
  auto parnt = std::make_shared<Media>();
  auto chld = std::make_shared<Media>();
  auto sqn = std::make_shared<Sequence>();
  chld->setSequence(sqn);
  QVERIFY(model.appendChild(parnt, chld) == true);
  QVERIFY(model.project_items.size() == count + 1);
}


void ProjectModelTest::testCaseGetFolder()
{
  ProjectModel model;
  QVERIFY(model.getFolder(1) == nullptr);
}

void ProjectModelTest::testCaseGetFolderPopulated()
{
  ProjectModel model;
  auto mda = std::make_shared<Media>();
  mda->setFolder();
  model.add(mda);
  QVERIFY(model.getFolder(1) == mda);
  QVERIFY(model.getFolder(0) == nullptr);
}
