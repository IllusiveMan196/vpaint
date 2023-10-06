// Copyright (C) 2012-2019 The VPaint Developers.
// See the COPYRIGHT file at the top-level directory of this distribution
// and at https://github.com/dalboris/vpaint/blob/master/COPYRIGHT
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <QTextStream>
#include <QtDebug>
#include <QMessageBox>
#include <array>
#include <vector>

#include "../SaveAndLoad.h"
#include "../Random.h"

#include "EdgeGeometry.h"

#include "KeyVertex.h"
#include "KeyEdge.h"
#include "KeyFace.h"
#include "InbetweenFace.h"
#include "VAC.h"
#include "../DevSettings.h"
#include "../Global.h"

#include "../XmlStreamReader.h"
#include "../XmlStreamWriter.h"

namespace VectorAnimationComplex
{

namespace
{

detail::PolygonData createPolygonData(const QList<AnimatedCycle> & cycles, Time time)
{
    detail::PolygonData vertices;
    for(int k=0; k<cycles.size(); ++k)      // for each cycle
    {
        vertices << std::vector< std::array<double, 3> >(); // create a contour data

        QList<Eigen::Vector2d> sampling;
        AnimatedCycle cycle = cycles[k];
        cycle.sample(time, sampling);
        for(int j=0; j<sampling.size(); ++j)
        {
            std::array<double, 3> a = {sampling[j][0], sampling[j][1], 0};
            vertices.back().emplace_back(a);
        }
    }

    return vertices;
}

void computeTrianglesFromCycles(const QList<AnimatedCycle> & cycles, Triangles & triangles, Time time)
{
    detail::PolygonData polygon = createPolygonData(cycles,time);
    detail::tesselatePolygon(polygon, triangles);
}

}

InbetweenFace::InbetweenFace(VAC * vac) :
    Cell(vac),
    InbetweenCell(vac),
    FaceCell(vac)
{
}

InbetweenFace::InbetweenFace(VAC * vac,
                             const QList<AnimatedCycle> & cycles,
                             const QSet<KeyFace*> & beforeFaces,
                             const QSet<KeyFace*> & afterFaces) :
    Cell(vac),
    InbetweenCell(vac),
    FaceCell(vac),
    cycles_(cycles),
    beforeFaces_(beforeFaces),
    afterFaces_(afterFaces)
{
    // TODO: Check invariants
    addMeToStarOfBoundary_();
}

void InbetweenFace::addAnimatedCycle() // invalid cycle
{
    cycles_ << AnimatedCycle();
}

void InbetweenFace::addAnimatedCycle(const AnimatedCycle & cycle) // must be valid
{
    addAnimatedCycle();
    setCycle(numAnimatedCycles()-1, cycle);
}

void InbetweenFace::setCycle(int i, const AnimatedCycle & cycle) // must be valid
{
    // TODO: Check invariants
    removeMeFromStarOfBoundary_();
    cycles_[i] = cycle;
    addMeToStarOfBoundary_();
    processGeometryChanged_();
}

void InbetweenFace::removeCycle(int i)
{
    removeMeFromStarOfBoundary_();
    cycles_.removeAt(i);
    addMeToStarOfBoundary_();
    processGeometryChanged_();
}

void InbetweenFace::setBeforeFaces(const QSet<KeyFace*> & beforeFaces)
{
    removeMeFromStarOfBoundary_();
    beforeFaces_ = beforeFaces;
    addMeToStarOfBoundary_();
}

void InbetweenFace::setAfterFaces(const QSet<KeyFace*> & afterFaces)
{
    removeMeFromStarOfBoundary_();
    afterFaces_ = afterFaces;
    addMeToStarOfBoundary_();
}

void InbetweenFace::addBeforeFace(KeyFace * beforeFace)
{
    beforeFaces_ << beforeFace;
    addMeToTemporalStarAfterOf_(beforeFace);
}

void InbetweenFace::addAfterFace(KeyFace * afterFace)
{
    afterFaces_ << afterFace;
    addMeToTemporalStarBeforeOf_(afterFace);
}

void InbetweenFace::removeBeforeFace(KeyFace * beforeFace)
{
    beforeFaces_.remove(beforeFace);
    removeMeFromTemporalStarAfterOf_(beforeFace);
}

void InbetweenFace::removeAfterFace(KeyFace * afterFace)
{
    afterFaces_.remove(afterFace);
    removeMeFromTemporalStarBeforeOf_(afterFace);
}

int InbetweenFace::numAnimatedCycles() const
{
    return cycles_.size();
}

AnimatedCycle InbetweenFace::animatedCycle(int i) const
{
    return cycles_[i];
}

QSet<KeyFace*> InbetweenFace::beforeFaces() const
{
    return beforeFaces_;
}

QSet<KeyFace*> InbetweenFace::afterFaces() const
{
    return afterFaces_;
}

void InbetweenFace::triangulate_(Time time, Triangles & out) const
{
    out.clear();
    if (exists(time))
        computeTrianglesFromCycles(cycles_, out, time);
}

QList<QList<Eigen::Vector2d> > InbetweenFace::getSampling(Time time) const
{
    QList<QList<Eigen::Vector2d> > res;
    detail::PolygonData data = createPolygonData(cycles_, time);

    for(unsigned int k=0; k<data.size(); ++k) // for each cycle
    {
        res << QList<Eigen::Vector2d>();
        for(unsigned int i=0; i<data[k].size(); ++i) // for each edge in the cycle
        {
            res[k] << Eigen::Vector2d(data[k][i][0], data[k][i][1]);
        }
    }

    return res;
}

InbetweenFace::InbetweenFace(VAC * g, QTextStream & in) :
    Cell(g,in),
    InbetweenCell(g, in),
    FaceCell(g, in)
{
    Field field;

    // ---- Cycles ----
    in >> field >> cycles_;

    // ---- Before faces ----
    in >> field;
    tempBeforeFaces_.clear();

    // put the list to read as a string
    QString listAsString;
    in >> listAsString; // read "["
    int openedBracket = 1;
    char c;
    while(openedBracket != 0)
    {
        in >> c;
        if(c == '[')
            openedBracket++;
        if(c == ']')
            openedBracket--;

        listAsString.append(c);
    }

    // test if the list is void
    QString copyString = listAsString;
    QTextStream test(&copyString);
    QString b1, b2;
    test >> b1 >> b2;

    // if not void
    if(b2 != "]")
    {
        QTextStream newIn(&listAsString);
        newIn >> b1; // read "["
        QString delimiter(",");
        while(delimiter == ",")
        {
            int id;
            newIn >> id;
            tempBeforeFaces_ << id;
            newIn >> delimiter;
        }
    }

    // ---- After faces ----
    in >> field;
    tempAfterFaces_.clear();

    // put the list to read as a string
    QString listAsString2;
    in >> listAsString2; // read "["
    int openedBracket2 = 1;
    char c2;
    while(openedBracket2 != 0)
    {
        in >> c2;
        if(c2 == '[')
            openedBracket2++;
        if(c2 == ']')
            openedBracket2--;

        listAsString2.append(c2);
    }

    // test if the list is void
    QString copyString2 = listAsString2;
    QTextStream test2(&copyString2);
    QString b12, b22;
    test2 >> b12 >> b22;

    // if not void
    if(b22 != "]")
    {
        QTextStream newIn2(&listAsString2);
        newIn2 >> b12; // read "["
        QString delimiter2(",");
        while(delimiter2 == ",")
        {
            int id2;
            newIn2 >> id2;
            tempAfterFaces_ << id2;
            newIn2 >> delimiter2;
        }
    }



}

void InbetweenFace::read2ndPass()
{
    // Base classes
    Cell::read2ndPass();
    InbetweenCell::read2ndPass();
    FaceCell::read2ndPass();

    // Cycles
    for(int i=0; i<cycles_.size(); ++i)
        cycles_[i].convertTempIdsToPointers(vac());

    // Before faces
    beforeFaces_.clear();
    foreach(int id, tempBeforeFaces_)
        beforeFaces_ << vac()->getCell(id)->toKeyFace();

    // After faces
    afterFaces_.clear();
    foreach(int id, tempAfterFaces_)
        afterFaces_ << vac()->getCell(id)->toKeyFace();
}

void InbetweenFace::save_(QTextStream & out)
{
    // Base classes
    Cell::save_(out);
    InbetweenCell::save_(out);
    FaceCell::save_(out);

    // Cycles
    out << Save::newField("Cycles") << cycles_;

    // Before faces
    out << Save::newField("BeforeFaces");
    out << "[";
    bool first = true;
    foreach(KeyFace * face, beforeFaces_)
    {
        if(first)
            first = false;
        else
            out << " ,";
        out << " " << face->id();
    }
    out << " ]";

    // After faces
    out << Save::newField("AfterFaces");
    out << "[";
    first = true;
    foreach(KeyFace * face, afterFaces_)
    {
        if(first)
            first = false;
        else
            out << " ,";
        out << " " << face->id();
    }
    out << " ]";
}

QString InbetweenFace::xmlType_() const
{
    return "inbetweenface";
}

void InbetweenFace::write_(XmlStreamWriter & xml) const
{
    // Base classes
    Cell::write_(xml);
    InbetweenCell::write_(xml);
    FaceCell::write_(xml);

    // Cycles
    QString cyclesstring;
    for(int i=0; i<cycles_.size(); ++i)
    {
        if(i>0)
            cyclesstring += " ";
        cyclesstring += cycles_[i].toString();
    }
    xml.writeAttribute("cycles", cyclesstring);

    // Before faces
    QString beforeFacesString;
    bool first = true;
    foreach(KeyFace * face, beforeFaces_)
    {
        if(first)
            first = false;
        else
            beforeFacesString += " ";

        beforeFacesString += QString().setNum(face->id());
    }
    xml.writeAttribute("beforefaces", beforeFacesString);

    // After faces
    QString afterFacesString;
    first = true;
    foreach(KeyFace * face, afterFaces_)
    {
        if(first)
            first = false;
        else
            afterFacesString += " ";

        afterFacesString += QString().setNum(face->id());
    }
    xml.writeAttribute("afterfaces", afterFacesString);
}

InbetweenFace::InbetweenFace(VAC * vac, XmlStreamReader & xml) :
    Cell(vac, xml),
    InbetweenCell(vac, xml),
    FaceCell(vac, xml)
{
    // Cycles
    QString str;
    QStringRef d = xml.attributes().value("cycles");
    bool opened = false;
    for(int i=0; i<d.length(); ++i)
    {
        QChar c = d.at(i);
        if(c == '[')
            opened = true;
        if(opened)
            str += c;
        if(c==']')
        {
            cycles_ << AnimatedCycle();
            cycles_.last().fromString(str);
            opened = false;
            str.clear();
        }
    }

    // Before faces
    QStringList beforefacesSL = xml.attributes().value("beforefaces").toString().simplified().split(' ', Qt::SkipEmptyParts);
    tempBeforeFaces_.clear();
    for(int i=0; i<beforefacesSL.size(); ++i)
        tempBeforeFaces_ << beforefacesSL[i].toInt();

    // After faces
    QStringList afterfacesSL = xml.attributes().value("afterfaces").toString().simplified().split(' ', Qt::SkipEmptyParts);
    tempAfterFaces_.clear();
    for(int i=0; i<afterfacesSL.size(); ++i)
        tempAfterFaces_ << afterfacesSL[i].toInt();
}

InbetweenFace::~InbetweenFace()
{
}

InbetweenFace * InbetweenFace::clone()
{
    return new InbetweenFace(this);
}

void InbetweenFace::remapPointers(VAC * newVAC)
{
    Cell::remapPointers(newVAC);
    InbetweenCell::remapPointers(newVAC);
    FaceCell::remapPointers(newVAC);

    // Cycles
    for(int i=0; i<cycles_.size(); ++i)
        cycles_[i].remapPointers(newVAC);

    // Before faces
    QSet<KeyFace*> newBeforeFaces;
    foreach(KeyFace * face, beforeFaces_)
        newBeforeFaces << vac()->getCell(face->id())->toKeyFace();
    beforeFaces_ = newBeforeFaces;

    // After faces
    QSet<KeyFace*> newAfterFaces;
    foreach(KeyFace * face, afterFaces_)
        newAfterFaces << vac()->getCell(face->id())->toKeyFace();
    afterFaces_ = newAfterFaces;
}


InbetweenFace::InbetweenFace(InbetweenFace * other):
    Cell(other),
    InbetweenCell(other),
    FaceCell(other)
{
    cycles_ = other->cycles_;
    beforeFaces_ = other->beforeFaces_;
    afterFaces_ = other->afterFaces_;
}

KeyCellSet InbetweenFace::beforeCells() const
{
    KeyCellSet res = beforeFaces_;
    for(int i=0; i<cycles_.size(); ++i)
        res.unite(cycles_[i].beforeCells());
    return res;
}
KeyCellSet InbetweenFace::afterCells() const
{
    KeyCellSet res = afterFaces_;
    for(int i=0; i<cycles_.size(); ++i)
        res.unite(cycles_[i].afterCells());
    return res;
}

CellSet InbetweenFace::spatialBoundary() const
{
    CellSet res;

    for(int i=0; i<cycles_.size(); ++i)
    {
        CellSet cells = cycles_[i].cells();
        res.unite(cells);
    }

    return res;
}

// Update Boundary
void InbetweenFace::updateBoundary_impl(KeyVertex * oldVertex, KeyVertex * newVertex)
{
    for(int i=0; i<cycles_.size(); ++i)
        cycles_[i].replaceVertex(oldVertex,newVertex);
}
void InbetweenFace::updateBoundary_impl(const KeyHalfedge & oldHalfedge, const KeyHalfedge & newHalfedge)
{
    for(int i=0; i<cycles_.size(); ++i)
        cycles_[i].replaceHalfedge(oldHalfedge,newHalfedge);
}
void InbetweenFace::updateBoundary_impl(KeyEdge * oldEdge, const KeyEdgeList & newEdges)
{
    // Nothing to do if old edge is in the temporal boundary (as opposed to in the spatial boundary)
    if(!exists(oldEdge->time()))
        return;

    for(int i=0; i<cycles_.size(); ++i)
        cycles_[i].replaceEdges(oldEdge, newEdges);
}


bool InbetweenFace::check_() const
{
    // todo
    return true;
}

}
