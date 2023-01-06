/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Data Visualization module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 or (at your option) any later version
** approved by the KDE Free Qt Foundation. The licenses are as published by
** the Free Software Foundation and appearing in the file LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "scatterdatamodifier.h"
#include <QtDataVisualization/qscatterdataproxy.h>
#include <QtDataVisualization/qvalue3daxis.h>
#include <QtDataVisualization/q3dscene.h>
#include <QtDataVisualization/q3dcamera.h>
#include <QtDataVisualization/qscatter3dseries.h>
#include <QtDataVisualization/q3dtheme.h>
#include <QtDataVisualization/QCustom3DItem>
#include <QtCore/qmath.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <unordered_map>


using namespace QtDataVisualization;

static const float verticalRange = 8.0f;
static const float horizontalRange = verticalRange;
static const float ellipse_a = horizontalRange / 3.0f;
static const float ellipse_b = verticalRange;
static const float doublePi = float(M_PI) * 2.0f;
static const float radiansToDegrees = 360.0f / doublePi;
static const float animationFrames = 30.0f;
static const std::unordered_map<std::string, float> atomRadii = {{"C", 0.69}, {"H", 0.31}};
std::vector<QCustom3DItem*> atoms;


ScatterDataModifier::ScatterDataModifier(Q3DScatter *scatter)
    : m_graph(scatter),
      m_fieldLines(12),
      m_arrowsPerLine(16),
      m_magneticField(new QScatter3DSeries),
      m_sun(new QCustom3DItem),
      num_atoms(18),
      atom(new QCustom3DItem),
      m_magneticFieldArray(0),
      m_angleOffset(0.0f),
      m_angleStep(doublePi / m_arrowsPerLine / animationFrames)
{

    // Initialize the atoms outside of the initialization list since it's a little bit unclear how to do so.
    for (std::vector<float> coords: getCarbonCoords()) {
        QCustom3DItem* a = new QCustom3DItem;

        a->setScaling(QVector3D(0.01f, 0.01f, 0.01f));
        a->setMeshFile(QStringLiteral(":/mesh/largesphere.obj"));
        QImage aColor = QImage(2, 2, QImage::Format_RGB32);
        aColor.fill(QColor(0, 0, 0));
        a->setTextureImage(aColor);
        float x = coords[0];
        float y = coords[1];
        float z = coords[2];
        a->setPosition(QVector3D(x, y, z));
        m_graph->addCustomItem(a);

        // Add the radius of the atom.
        QCustom3DItem* radius = new QCustom3DItem;
        radius->setScaling(QVector3D(0.025f, 0.025f, 0.025f));
        radius->setMeshFile(QStringLiteral(":/mesh/largesphere.obj"));
        QImage rColor = QImage(2, 2, QImage::Format_RGB32);
        rColor.fill(QColor(5, 5, 5));
        radius->setTextureImage(rColor);
        radius->setPosition(QVector3D(x, y, z));
        radius->setVisible(false);
        atoms.push_back(radius);
        m_graph->addCustomItem(radius);

    }

    for (std::vector<float> coords: getHydrogenCoords()) {
        QCustom3DItem* a = new QCustom3DItem;

        a->setScaling(QVector3D(0.005f, 0.005f, 0.005f));
        a->setMeshFile(QStringLiteral(":/mesh/largesphere.obj"));
        QImage aColor = QImage(2, 2, QImage::Format_RGB32);
        aColor.fill(QColor(220, 220, 220));
        a->setTextureImage(aColor);
        float x = coords[0];
        float y = coords[1];
        float z = coords[2];
        a->setPosition(QVector3D(x, y, z));
        m_graph->addCustomItem(a);

        // Add the radius of the atom.
        QCustom3DItem* radius = new QCustom3DItem;
        radius->setScaling(QVector3D(0.015f, 0.015f, 0.015f));
        radius->setMeshFile(QStringLiteral(":/mesh/largesphere.obj"));
        QImage rColor = QImage(2, 2, QImage::Format_RGB32);
        rColor.fill(QColor(225, 225, 225));
        radius->setTextureImage(rColor);
        radius->setPosition(QVector3D(x, y, z));
        radius->setVisible(false);
        atoms.push_back(radius);
        m_graph->addCustomItem(radius);


    }

    this -> atoms = atoms;

    m_graph->setShadowQuality(QAbstract3DGraph::ShadowQualityNone);
    m_graph->scene()->activeCamera()->setCameraPreset(Q3DCamera::CameraPresetFront);

    // Configure the axes according to the data
    m_graph->axisX()->setRange(-horizontalRange, horizontalRange);
    m_graph->axisY()->setRange(-verticalRange, verticalRange);
    m_graph->axisZ()->setRange(-horizontalRange, horizontalRange);
    m_graph->axisX()->setSegmentCount(int(horizontalRange));
    m_graph->axisZ()->setSegmentCount(int(horizontalRange));

    QObject::connect(&m_rotationTimer, &QTimer::timeout, this,
                     &ScatterDataModifier::triggerRotation);

    toggleRotation();
    generateData();
}

ScatterDataModifier::~ScatterDataModifier()
{
    delete m_graph;
}

void ScatterDataModifier::generateData()
{
    // Reusing existing array is computationally cheaper than always generating new array, even if
    // all data items change in the array, if the array size doesn't change.
    if (!m_magneticFieldArray)
        m_magneticFieldArray = new QScatterDataArray;

    int arraySize = m_fieldLines * m_arrowsPerLine;
    if (arraySize != m_magneticFieldArray->size())
        m_magneticFieldArray->resize(arraySize);

    QScatterDataItem *ptrToDataArray = &m_magneticFieldArray->first();

    for (int i = 0; i < m_fieldLines; i++) {
        float horizontalAngle = (doublePi * i) / m_fieldLines;
        float xCenter = ellipse_a * qCos(horizontalAngle);
        float zCenter = ellipse_a * qSin(horizontalAngle);

        // Rotate - arrow always tangential to origin
        //! [0]
        QQuaternion yRotation = QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, horizontalAngle * radiansToDegrees);
        //! [0]

        for (int j = 0; j < m_arrowsPerLine; j++) {
            // Calculate point on ellipse centered on origin and parallel to x-axis
            float verticalAngle = ((doublePi * j) / m_arrowsPerLine) + m_angleOffset;
            float xUnrotated = ellipse_a * qCos(verticalAngle);
            float y = ellipse_b * qSin(verticalAngle);

            // Rotate the ellipse around y-axis
            float xRotated = xUnrotated * qCos(horizontalAngle);
            float zRotated = xUnrotated * qSin(horizontalAngle);

            // Add offset
            float x = xCenter + xRotated;
            float z = zCenter + zRotated;

            //! [1]
            QQuaternion zRotation = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, verticalAngle * radiansToDegrees);
            QQuaternion totalRotation = yRotation * zRotation;
            //! [1]

            ptrToDataArray->setPosition(QVector3D(x, y, z));
            //! [2]
            ptrToDataArray->setRotation(totalRotation);
            //! [2]
            ptrToDataArray++;
        }
    }

    if (m_graph->selectedSeries() == m_magneticField)
        m_graph->clearSelection();

    m_magneticField->dataProxy()->resetArray(m_magneticFieldArray);
}

std::vector<std::vector<float>> ScatterDataModifier::getCarbonCoords() {
    std::ifstream xyzFile("/Users/josephgmaa/Qt/Examples/Qt-5.15.2/datavisualization/rotations/molecules/cyclohexane.xyz");
    std::string line;
    std::vector<std::vector<float>> carbonCoords;
    std::string atom_type;
    float x, y, z;
    getline(xyzFile, line);
    getline(xyzFile, line);
    // Read in the first line that has the number of atoms to follow.
    while (getline(xyzFile, line)) {
        std::vector<float> coords;
        std::istringstream split_line(line);
        split_line >> atom_type >> x >> y >> z;

        if (atom_type == "C") {
            coords.push_back(x);
            coords.push_back(y);
            coords.push_back(z);
            carbonCoords.push_back(coords);
        }
    }

    return carbonCoords;
}

std::vector<std::vector<float>> ScatterDataModifier::getHydrogenCoords() {
    std::ifstream xyzFile("/Users/josephgmaa/Qt/Examples/Qt-5.15.2/datavisualization/rotations/molecules/cyclohexane.xyz");
    std::string line;
    std::vector<std::vector<float>> hydrogenCoords;
    std::string atom_type;
    float x, y, z;
    getline(xyzFile, line);
    getline(xyzFile, line);
    // Read in the first line that has the number of atoms to follow.
    while (getline(xyzFile, line)) {
        std::vector<float> coords;
        std::istringstream split_line(line);
        split_line >> atom_type >> x >> y >> z;
        if (atom_type == "H") {
            coords.push_back(x);
            coords.push_back(y);
            coords.push_back(z);
            hydrogenCoords.push_back(coords);
        }
    }

    return hydrogenCoords;
}

void ScatterDataModifier::setFieldLines(int lines)
{
    m_fieldLines = lines;
    generateData();
}

void ScatterDataModifier::setArrowsPerLine(int arrows)
{
    m_angleOffset = 0.0f;
    m_angleStep = doublePi / m_arrowsPerLine / animationFrames;
    m_arrowsPerLine = arrows;
    generateData();
}

void ScatterDataModifier::triggerRotation()
{
    m_angleOffset += m_angleStep;
    generateData();
}

void ScatterDataModifier::toggleRadii()
{
    for (auto& r: atoms) {
        r -> setVisible((!r->isVisible()));
    }
}

void ScatterDataModifier::toggleRotation()
{
    if (m_rotationTimer.isActive())
        m_rotationTimer.stop();
    else
        m_rotationTimer.start(15);
}
