#include "Drawing.h"
#include "IBKMK_3DCalculations.h"
#include "Constants.h"

#include "IBK_MessageHandler.h"
#include "IBK_messages.h"
#include "IBK_physics.h"

#include "Utilities.h"
#include "ext/matrix_transform.hpp"
#include "qfont.h"
#include "qpainterpath.h"
#include <tinyxml.h>


static int PRECISION = 16;  // precision of floating point values for output writing

/*! IBKMK::Vector3D to QVector3D conversion macro. */
inline QVector3D IBKVector2QVector(const IBKMK::Vector3D & v) {
	return QVector3D((float)v.m_x, (float)v.m_y, (float)v.m_z);
}

/*! QVector3D to IBKMK::Vector3D to conversion macro. */
inline IBKMK::Vector3D QVector2IBKVector(const QVector3D & v) {
	return IBKMK::Vector3D((double)v.x(), (double)v.y(), (double)v.z());
}

Drawing::Drawing() :
	m_blocks(std::vector<Block>()),
	m_drawingLayers(std::vector<DrawingLayer>()),
	m_points(std::vector<Point>()),
	m_lines(std::vector<Line>()),
	m_polylines(std::vector<PolyLine>()),
	m_circles(std::vector<Circle>()),
	m_ellipses(std::vector<Ellipse>()),
	m_arcs(std::vector<Arc>()),
	m_solids(std::vector<Solid>()),
	m_texts(std::vector<Text>())
{}


TiXmlElement * Drawing::Text::writeXMLPrivate(TiXmlElement * parent) const {
	if (m_id == INVALID_ID)  return nullptr;

	TiXmlElement * e = new TiXmlElement("Text");
	parent->LinkEndChild(e);

	if (m_id != INVALID_ID)
		e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (m_color.isValid())
		e->SetAttribute("color", m_color.name().toStdString());
	if (m_zPosition != 0.0)
		e->SetAttribute("zPosition", IBK::val2string<unsigned int>(m_zPosition));
	if (!m_text.isEmpty())
		e->SetAttribute("text", m_text.toStdString());
	if (!m_layerName.isEmpty())
		e->SetAttribute("layer", m_layerName.toStdString());
	if (m_rotationAngle != 0.0)
		e->SetAttribute("rotationAngle", IBK::val2string<double>(m_rotationAngle));
	if (m_height != 10.0)
		e->SetAttribute("height", IBK::val2string<double>(m_height));
	if (!m_blockName.isEmpty())
		e->SetAttribute("blockName", m_blockName.toStdString());

	TiXmlElement::appendSingleAttributeElement(e, "BasePoint", nullptr, std::string(), m_basePoint.toString(PRECISION));

	return e;
}

void Drawing::Text::readXMLPrivate(const TiXmlElement *element){
	FUNCID(Drawing::Text::readXMLPrivate);

	try {
		// search for mandatory attributes
		if (!TiXmlAttribute::attributeByName(element, "id")) {
			IBK::IBK_Message( IBK::FormatString(XML_READ_ERROR).arg(element->Row()).arg(
								  IBK::FormatString("Missing required 'id' attribute.") ), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			return;
		}

		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "id")
				m_id = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "color")
				m_color = QColor(QString::fromStdString(attrib->ValueStr()));
			else if (attribName == "zPosition")
				m_zPosition = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "text")
				m_text = QString::fromStdString(attrib->ValueStr());
			else if (attribName == "layer")
				m_layerName = QString::fromStdString(attrib->ValueStr());
			else if (attribName == "height")
				m_height = readPODAttributeValue<double>(element, attrib);
			else if (attribName == "rotationAngle")
				m_rotationAngle = readPODAttributeValue<double>(element, attrib);
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ATTRIBUTE).arg(attribName).arg(element->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			attrib = attrib->Next();
		}

		// reading elements
		const TiXmlElement * c = element->FirstChildElement();
		while (c) {
			const std::string & cName = c->ValueStr();
			if (cName == "BasePoint") {
				try {
					m_basePoint = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(cName).arg(c->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			c = c->NextSiblingElement();
		}
	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'Drawing::Text' element."), FUNC_ID);
	}
	catch (std::exception & ex2) {
		throw IBK::Exception( IBK::FormatString("%1\nError reading 'Drawing::Text' element.").arg(ex2.what()), FUNC_ID);
	}
}

const std::vector<IBKMK::Vector2D> &Drawing::Text::points2D() const {
	if (m_dirtyLocalPoints) {
		m_pickPoints.clear();
		m_pickPoints.push_back(m_basePoint);
	}
	return m_pickPoints;
}

const std::vector<Drawing::LineSegment> &Drawing::Text::lineGeometries() const {
	FUNCID(Drawing::Text::planeGeometries);
	try {
		if (m_dirtyGlobalPoints) {
			m_lineGeometries.clear();
			drawing()->generateLinesFromText(m_text.toStdString(),
											 m_height, m_alignment, - m_rotationAngle, m_basePoint,
											 *this, m_lineGeometries);

			m_dirtyGlobalPoints = false;
		}

		return m_lineGeometries;
	} catch (IBK::Exception &ex) {
		throw IBK::Exception(IBK::FormatString("Could not generate plane geometries of 'Drawing::Text'\n%1").arg(ex.what()), FUNC_ID);
	}

}


TiXmlElement * Drawing::Solid::writeXMLPrivate(TiXmlElement * parent) const {
	if (m_id == INVALID_ID)  return nullptr;

	TiXmlElement * e = new TiXmlElement("Solid");
	parent->LinkEndChild(e);

	if (m_id != INVALID_ID)
		e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (m_color.isValid())
		e->SetAttribute("color", m_color.name().toStdString());
	if (m_zPosition != 0.0)
		e->SetAttribute("zPosition", IBK::val2string<unsigned int>(m_zPosition));
	if (!m_layerName.isEmpty())
		e->SetAttribute("layer", m_layerName.toStdString());

	TiXmlElement::appendSingleAttributeElement(e, "Point1", nullptr, std::string(), m_point1.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "Point2", nullptr, std::string(), m_point2.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "Point3", nullptr, std::string(), m_point3.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "Point4", nullptr, std::string(), m_point4.toString(PRECISION));

	return e;
}

void Drawing::Solid::readXMLPrivate(const TiXmlElement *element){
	FUNCID(Drawing::Solid::readXMLPrivate);

	try {
		// search for mandatory attributes
		if (!TiXmlAttribute::attributeByName(element, "id")) {
			IBK::IBK_Message( IBK::FormatString(XML_READ_ERROR).arg(element->Row()).arg(
								  IBK::FormatString("Missing required 'id' attribute.") ), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			return;
		}

		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "id")
				m_id = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "color")
				m_color = QColor(QString::fromStdString(attrib->ValueStr()));
			else if (attribName == "zPosition")
				m_zPosition = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "layer")
				m_layerName = QString::fromStdString(attrib->ValueStr());
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ATTRIBUTE).arg(attribName).arg(element->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			attrib = attrib->Next();
		}

		// reading elements
		const TiXmlElement * c = element->FirstChildElement();
		while (c) {
			const std::string & cName = c->ValueStr();
			if (cName == "Point1") {
				try {
					m_point1 = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else if (cName == "Point2") {
				try {
					m_point2 = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else if (cName == "Point3") {
				try {
					m_point3 = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else if (cName == "Point4") {
				try {
					m_point4 = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(cName).arg(c->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			c = c->NextSiblingElement();
		}
	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'Drawing::Solid' element."), FUNC_ID);
	}
}

const std::vector<IBKMK::Vector2D> &Drawing::Solid::points2D() const {
	return m_pickPoints;
}

const std::vector<Drawing::LineSegment> &Drawing::Solid::lineGeometries() const {
	FUNCID(Drawing::Line::planeGeometries);
	try {
		if (m_dirtyGlobalPoints) {
			m_lineGeometries.clear();

			const Drawing *drawing = this->drawing();
			Q_ASSERT(drawing);

			std::vector<IBKMK::Vector2D> points2D {m_point1, m_point2, m_point3, m_point4};
			std::vector<IBKMK::Vector3D> verts = drawing->points3D(points2D, *this);

			for (unsigned int i = 0; i < 4; ++i) {
				const IBKMK::Vector3D &p1 = verts[ i				   ];
				const IBKMK::Vector3D &p2 = verts[(i+1) % verts.size()];

				m_lineGeometries.push_back(LineSegment(p2, p1));
			}

			m_dirtyGlobalPoints = false;
		}

		return m_lineGeometries;
	} catch (IBK::Exception &ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error generating plane geometries for 'Drawing::Solid' element.\n%1").arg(ex.what()), FUNC_ID);
	}
}

TiXmlElement * Drawing::LinearDimension::writeXMLPrivate(TiXmlElement * parent) const {
	if (m_id == INVALID_ID)  return nullptr;

	TiXmlElement * e = new TiXmlElement("LinearDimension");
	parent->LinkEndChild(e);

	if (m_id != INVALID_ID)
		e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (m_color.isValid())
		e->SetAttribute("color", m_color.name().toStdString());
	if (m_zPosition != 0.0)
		e->SetAttribute("zPosition", IBK::val2string<unsigned int>(m_zPosition));
	if (m_angle != 0.0)
		e->SetAttribute("angle", IBK::val2string<double>(m_angle));
	if (m_measurement != "")
		e->SetAttribute("measurement", m_measurement.toStdString());
	if (!m_layerName.isEmpty())
		e->SetAttribute("layer", m_layerName.toStdString());
	if (!m_styleName.isEmpty())
		e->SetAttribute("styleName", m_styleName.toStdString());

	TiXmlElement::appendSingleAttributeElement(e, "Point1", nullptr, std::string(), m_point1.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "Point2", nullptr, std::string(), m_point2.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "DimensionPoint", nullptr, std::string(), m_dimensionPoint.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "LeftPoint", nullptr, std::string(), m_leftPoint.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "RightPoint", nullptr, std::string(), m_rightPoint.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "TextPoint", nullptr, std::string(), m_textPoint.toString(PRECISION));

	return e;
}

void Drawing::LinearDimension::readXMLPrivate(const TiXmlElement *element){
	FUNCID(Drawing::LinearDimension::readXMLPrivate);

	try {
		// search for mandatory attributes
		if (!TiXmlAttribute::attributeByName(element, "id")) {
			IBK::IBK_Message( IBK::FormatString(XML_READ_ERROR).arg(element->Row()).arg(
								  IBK::FormatString("Missing required 'id' attribute.") ), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			return;
		}

		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "id")
				m_id = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "color")
				m_color = QColor(QString::fromStdString(attrib->ValueStr()));
			else if (attribName == "zPosition")
				m_zPosition = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "angle")
				m_angle = readPODAttributeValue<double>(element, attrib);
			else if (attribName == "measurement")
				m_measurement = QString::fromStdString(attrib->ValueStr());
			else if (attribName == "layer")
				m_layerName = QString::fromStdString(attrib->ValueStr());
			else if (attribName == "styleName")
				m_styleName = QString::fromStdString(attrib->ValueStr());
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ATTRIBUTE).arg(attribName).arg(element->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			attrib = attrib->Next();
		}

		// reading elements
		const TiXmlElement * c = element->FirstChildElement();
		while (c) {
			const std::string & cName = c->ValueStr();
			if (cName == "Point1") {
				try {
					m_point1 = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else if (cName == "Point2") {
				try {
					m_point2 = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else if (cName == "DimensionPoint") {
				try {
					m_dimensionPoint = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else if (cName == "LeftPoint") {
				try {
					m_leftPoint = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else if (cName == "RightPoint") {
				try {
					m_rightPoint = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}

			else if (cName == "TextPoint") {
				try {
					m_textPoint = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(cName).arg(c->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			c = c->NextSiblingElement();
		}
	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'Drawing::LinearDimension' element."), FUNC_ID);
	}
	catch (std::exception & ex2) {
		throw IBK::Exception( IBK::FormatString("%1\nError reading 'Drawing::LinearDimension' element.").arg(ex2.what()), FUNC_ID);
	}
}

const std::vector<IBKMK::Vector2D> &Drawing::LinearDimension::points2D() const {
	// special handling
	// is populated in planeGeometries
	return m_pickPoints;
}

const std::vector<Drawing::LineSegment> &Drawing::LinearDimension::lineGeometries() const {
	if (m_dirtyGlobalPoints) {
		m_lineGeometries.clear();

		if ((m_point1 - m_point2).magnitudeSquared() < 1E-2)
			return m_lineGeometries;

		const Drawing *drawing = this->drawing();

		// Create Vector from start and end point of the line, add point of origin to each coordinate and calculate z value
		m_pickPoints.push_back(m_leftPoint);
		m_pickPoints.push_back(m_rightPoint);

		// *** DIMENSION LINE  ***

		// left + right
		const IBKMK::Vector3D p1DimLine = m_parent->point3D(m_leftPoint, *this);
		const IBKMK::Vector3D p2DimLine = m_parent->point3D(m_rightPoint, *this);

		m_lineGeometries.push_back(LineSegment(p1DimLine, p2DimLine));

		// *** LEFT LINE  ***

		IBKMK::Vector2D l (m_leftPoint - m_point1);
		IBKMK::Vector2D point;
		if (m_style->m_fixedExtensionLength) {
			IBKMK::Vector2D ext = m_style->m_fixedExtensionLength * l.normalized();
			point.m_x = m_leftPoint.m_x - ext.m_x;
			point.m_y = m_leftPoint.m_y - ext.m_y;
		}
		else {
			IBKMK::Vector2D ext = m_style->m_extensionLineLowerDistance * l.normalized();
			point.m_x = m_point1.m_x + ext.m_x;
			point.m_y = m_point1.m_y + ext.m_y;
		}

		IBKMK::Vector2D lowerExtension = m_style->m_upperLineDistance * l.normalized();


		IBKMK::Vector3D p1Left = m_parent->point3D(point, *this);
		IBKMK::Vector3D p2Left = m_parent->point3D(m_leftPoint + lowerExtension, *this);

		m_lineGeometries.push_back(LineSegment(p1Left, p2Left));

		// *** RIGHT LINE ***

		IBKMK::Vector3D r (m_rightPoint - m_point2);
		if (m_style->m_fixedExtensionLength) {
			IBKMK::Vector3D ext = drawing->m_scalingFactor * m_style->m_fixedExtensionLength * r.normalized();
			point.m_x = m_rightPoint.m_x - ext.m_x;
			point.m_y = m_rightPoint.m_y - ext.m_y;
		}
		else {
			IBKMK::Vector3D ext = drawing->m_scalingFactor * m_style->m_extensionLineLowerDistance * r.normalized();
			point.m_x = m_point2.m_x + ext.m_x;
			point.m_y = m_point2.m_y + ext.m_y;
		}

		IBKMK::Vector3D p1Right = m_parent->point3D(point, *this);
		IBKMK::Vector3D p2Right = m_parent->point3D(m_rightPoint + lowerExtension, *this);

		m_lineGeometries.push_back(LineSegment(p1Right, p2Right));

		// *** TEXT ***

		double length = (m_leftPoint - m_rightPoint).magnitude();
		m_pickPoints.push_back(m_textPoint);

		QString measurementText;
		if (m_measurement == "") {
			measurementText = QString("%1").arg(length / m_style->m_textLinearFactor,
												m_style->m_textDecimalPlaces, 'g');
		}
		else
			measurementText = m_measurement;

		drawing->generateLinesFromText(measurementText.toStdString(),
									   m_style->m_textHeight * m_style->m_globalScalingFactor * 2,
									   Qt::AlignHCenter, m_angle, m_textPoint,
									   *this, m_lineGeometries);

		m_dirtyGlobalPoints = false;
		m_dirtyLocalPoints = false;
	}

	return m_lineGeometries;
}


TiXmlElement * Drawing::Point::writeXMLPrivate(TiXmlElement * parent) const {
	if (m_id == INVALID_ID)  return nullptr;

	TiXmlElement * e = new TiXmlElement("Point");
	parent->LinkEndChild(e);

	if (m_id != INVALID_ID)
		e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (m_color.isValid())
		e->SetAttribute("color", m_color.name().toStdString());
	if (m_zPosition != 0.0)
		e->SetAttribute("zPosition", IBK::val2string<unsigned int>(m_zPosition));
	if (!m_layerName.isEmpty())
		e->SetAttribute("layer", m_layerName.toStdString());

	TiXmlElement::appendSingleAttributeElement(e, "Point", nullptr, std::string(), m_point.toString(PRECISION));

	return e;
}

void Drawing::Point::readXMLPrivate(const TiXmlElement *element){
	FUNCID(Drawing::Circle::readXMLPrivate);

	try {
		// search for mandatory attributes
		if (!TiXmlAttribute::attributeByName(element, "id")) {
			IBK::IBK_Message( IBK::FormatString(XML_READ_ERROR).arg(element->Row()).arg(
								  IBK::FormatString("Missing required 'id' attribute.") ), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			return;
		}


		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "id")
				m_id = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "color")
				m_color = QColor(QString::fromStdString(attrib->ValueStr()));
			else if (attribName == "zPosition")
				m_zPosition = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "layer")
				m_layerName = QString::fromStdString(attrib->ValueStr());
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ATTRIBUTE).arg(attribName).arg(element->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			attrib = attrib->Next();
		}

		// reading elements
		const TiXmlElement * c = element->FirstChildElement();
		while (c) {
			const std::string & cName = c->ValueStr();
			if (cName == "Point") {
				try {
					m_point = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(cName).arg(c->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			c = c->NextSiblingElement();
		}
	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'Drawing::Point' element."), FUNC_ID);
	}
	catch (std::exception & ex2) {
		throw IBK::Exception( IBK::FormatString("%1\nError reading 'Drawing::Point' element.").arg(ex2.what()), FUNC_ID);
	}
}

const std::vector<IBKMK::Vector2D> &Drawing::Point::points2D() const {
	if (m_dirtyLocalPoints) {
		m_pickPoints.clear();
		m_pickPoints.push_back(m_point);

		m_dirtyLocalPoints = false;
	}

	return m_pickPoints;
}

const std::vector<Drawing::LineSegment> &Drawing::Point::lineGeometries() const {
	FUNCID(Drawing::Line::planeGeometries);
	try {
		// if (m_dirtyGlobalPoints) {
		// 	m_lineGeometries.clear();

		// 	// Create Vector from point, add point of origin to each coordinate and calculate z value
		// 	IBKMK::Vector3D p(drawing()->m_scalingFactor * m_point.m_x,
		// 					  drawing()->m_scalingFactor * m_point.m_y,
		// 					  m_zPosition * Z_MULTIPLYER);

		// 	// scale Vector with selected unit
		// 	double pointWeight = (drawing()->m_lineWeightOffset + lineWeight() * drawing()->m_lineWeightScaling) / 2;

		// 	// rotation
		// 	QVector3D vec = drawing()->m_rotationMatrix.toQuaternion() * IBKVector2QVector(p);
		// 	vec += IBKVector2QVector(drawing()->m_origin);

		// 	IBKMK::Vector3D p1 = QVector2IBKVector(vec);

		// 	IBKMK::Vector3D pExt0 = IBKMK::Vector3D(p1.m_x - pointWeight, p1.m_y - pointWeight, p1.m_z);
		// 	IBKMK::Vector3D pExt1 = IBKMK::Vector3D(p1.m_x + pointWeight, p1.m_y - pointWeight, p1.m_z);
		// 	IBKMK::Vector3D pExt2 = IBKMK::Vector3D(p1.m_x - pointWeight, p1.m_y + pointWeight, p1.m_z);

		// 	IBKMK::Polygon3D po(Polygon2D::T_Rectangle, pExt0, pExt2, pExt1);

		// 	for (const IBKMK::Vector3D &v3D : po.vertexes())
		// 		const_cast<IBKMK::Vector3D &>(v3D) = QVector2IBKVector(QMatrix4x4() * IBKVector2QVector(v3D));


		// 	m_lineGeometries.push_back(PlaneGeometry(po));
		// 	m_dirtyGlobalPoints = false;
		// }

		return m_lineGeometries;
	} catch (IBK::Exception &ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error generating plane geometries for 'Drawing::Point' element.\n%1").arg(ex.what()), FUNC_ID);
	}
}

void Drawing::Line::readXMLPrivate(const TiXmlElement *element){
	FUNCID(Drawing::Circle::readXMLPrivate);

	try {
		// search for mandatory attributes
		if (!TiXmlAttribute::attributeByName(element, "id")) {
			IBK::IBK_Message( IBK::FormatString(XML_READ_ERROR).arg(element->Row()).arg(
								  IBK::FormatString("Missing required 'id' attribute.") ), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			return;
		}

		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "id")
				m_id = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "color")
				m_color = QColor(QString::fromStdString(attrib->ValueStr()));
			else if (attribName == "zPosition")
				m_zPosition = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "layer")
				m_layerName = QString::fromStdString(attrib->ValueStr());
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ATTRIBUTE).arg(attribName).arg(element->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			attrib = attrib->Next();
		}

		// reading elements
		const TiXmlElement * c = element->FirstChildElement();
		while (c) {
			const std::string & cName = c->ValueStr();
			if (cName == "Point1") {
				try {
					m_point1 = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else if (cName == "Point2") {
				try {
					m_point2 = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(cName).arg(c->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			c = c->NextSiblingElement();
		}
	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'Drawing::Line' element."), FUNC_ID);
	}
	catch (std::exception & ex2) {
		throw IBK::Exception( IBK::FormatString("%1\nError reading 'Drawing::Line' element.").arg(ex2.what()), FUNC_ID);
	}
}

const std::vector<IBKMK::Vector2D> &Drawing::Line::points2D() const {
	if (m_dirtyLocalPoints) {
		m_pickPoints.clear();

		m_pickPoints.push_back(m_point1);
		m_pickPoints.push_back(m_point2);

		m_dirtyLocalPoints = false;
	}

	return m_pickPoints;
}

const std::vector<Drawing::LineSegment> &Drawing::Line::lineGeometries() const {
	if (m_dirtyGlobalPoints) {
		m_lineGeometries.clear();

		const Drawing *drawing = this->drawing();
		std::vector<IBKMK::Vector2D> verts = points2D();
		const std::vector<IBKMK::Vector3D> &points3D = drawing->points3D(verts, *this);

		if (points3D[0] != points3D[1])
			m_lineGeometries.push_back(LineSegment(points3D[0], points3D[1]));

		// if (!success)
		// 	IBK::IBK_Message(IBK::FormatString("Could not generate plane from line #%1").arg(m_id), IBK::MSG_WARNING);

		m_dirtyGlobalPoints = false;
	}

	return m_lineGeometries;
}

TiXmlElement * Drawing::Line::writeXMLPrivate(TiXmlElement * parent) const {
	if (m_id == INVALID_ID)  return nullptr;

	TiXmlElement * e = new TiXmlElement("Line");
	parent->LinkEndChild(e);

	if (m_id != INVALID_ID)
		e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (m_zPosition != 0.0)
		e->SetAttribute("zPosition", IBK::val2string<unsigned int>(m_zPosition));
	if (m_color.isValid())
		e->SetAttribute("color", m_color.name().toStdString());
	if (!m_layerName.isEmpty())
		e->SetAttribute("layer", m_layerName.toStdString());

	TiXmlElement::appendSingleAttributeElement(e, "Point1", nullptr, std::string(), m_point1.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "Point2", nullptr, std::string(), m_point2.toString(PRECISION));

	return e;
}


TiXmlElement * Drawing::Circle::writeXMLPrivate(TiXmlElement * parent) const {
	if (m_id == INVALID_ID)  return nullptr;

	TiXmlElement * e = new TiXmlElement("Circle");
	parent->LinkEndChild(e);

	if (m_id != INVALID_ID)
		e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (m_zPosition != 0.0)
		e->SetAttribute("zPosition", IBK::val2string<double>(m_id));
	if (m_color.isValid())
		e->SetAttribute("color", m_color.name().toStdString());
	if (!m_layerName.isEmpty())
		e->SetAttribute("layer", m_layerName.toStdString());

	TiXmlElement::appendSingleAttributeElement(e, "Center", nullptr, std::string(), m_center.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "Radius", nullptr, std::string(), IBK::val2string<double>(m_radius, PRECISION));

	return e;
}

void Drawing::Circle::readXMLPrivate(const TiXmlElement *element){
	FUNCID(Drawing::Circle::readXMLPrivate);

	try {
		// search for mandatory attributes
		if (!TiXmlAttribute::attributeByName(element, "id")) {
			IBK::IBK_Message( IBK::FormatString(XML_READ_ERROR).arg(element->Row()).arg(
								  IBK::FormatString("Missing required 'id' attribute.") ), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			return;
		}

		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "id")
				m_id = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "color")
				m_color = QColor(QString::fromStdString(attrib->ValueStr()));
			else if (attribName == "zPosition")
				m_zPosition = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "layer")
				m_layerName = QString::fromStdString(attrib->ValueStr());
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ATTRIBUTE).arg(attribName).arg(element->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			attrib = attrib->Next();
		}

		// reading elements
		const TiXmlElement * c = element->FirstChildElement();
		while (c) {
			const std::string & cName = c->ValueStr();
			if (cName == "Radius")
				m_radius = readPODElement<double>(c, cName);
			else if (cName == "Center") {
				try {
					m_center = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(cName).arg(c->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			c = c->NextSiblingElement();
		}
	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'Drawing::Circle' element."), FUNC_ID);
	}
	catch (std::exception & ex2) {
		throw IBK::Exception( IBK::FormatString("%1\nError reading 'Drawing::Circle' element.").arg(ex2.what()), FUNC_ID);
	}
}

const std::vector<IBKMK::Vector2D>& Drawing::Circle::points2D() const {
	if (m_dirtyLocalPoints) {
		m_pickPoints.clear();

		unsigned int count = SEGMENT_COUNT_CIRCLE * std::max(1, (int)(m_parent->m_scalingFactor * m_radius/15.0));
		m_pickPoints.resize(count);

		for(unsigned int i = 0; i < count; ++i){
			m_pickPoints[i] =IBKMK::Vector2D(m_center.m_x + m_radius * cos(2 * IBK::PI * i / count),
											 m_center.m_y + m_radius * sin(2 * IBK::PI * i / count));
		}

		m_dirtyLocalPoints = false;
	}

	return m_pickPoints;
}


const std::vector<Drawing::LineSegment> &Drawing::Circle::lineGeometries() const {
	FUNCID(Drawing::Circle::planeGeometries);
	try {
		if (m_dirtyGlobalPoints) {
			m_lineGeometries.clear();

			std::vector<IBKMK::Vector2D> points(points2D());
			for(unsigned int i = 0; i < points.size(); i++){
				const IBKMK::Vector3D &p1 = m_parent->point3D(points[  i					], *this);
				const IBKMK::Vector3D &p2 = m_parent->point3D(points[ (i+1) % points.size() ], *this);

				m_lineGeometries.push_back(LineSegment(p1, p2));
			}

			m_dirtyGlobalPoints = false;
		}

		return m_lineGeometries;
	} catch (IBK::Exception &ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error generating plane geometries for 'Drawing::Circle' element.\n%1").arg(ex.what()), FUNC_ID);

	}
}


TiXmlElement * Drawing::PolyLine::writeXMLPrivate(TiXmlElement * parent) const {
	if (m_id == INVALID_ID)  return nullptr;

	TiXmlElement * e = new TiXmlElement("PolyLine");
	parent->LinkEndChild(e);

	if (!m_polyline.empty()) {

		if (m_id != INVALID_ID)
			e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
		if (m_color.isValid())
			e->SetAttribute("color", m_color.name().toStdString());
		if (m_zPosition != 0.0)
			e->SetAttribute("zPosition", IBK::val2string<unsigned int>(m_zPosition));
		if (m_endConnected)
			e->SetAttribute("connected", IBK::val2string<bool>(m_endConnected));
		if (!m_layerName.isEmpty())
			e->SetAttribute("layer", m_layerName.toStdString());

		std::stringstream vals;
		const std::vector<IBKMK::Vector2D> & polyVertexes = m_polyline;
		for (unsigned int i=0; i<polyVertexes.size(); ++i) {
			vals << polyVertexes[i].toString(PRECISION);
			if (i<polyVertexes.size()-1)  vals << ", ";
		}
		TiXmlText * text = new TiXmlText( vals.str() );
		e->LinkEndChild( text );
	}

	return e;
}

void Drawing::PolyLine::readXMLPrivate(const TiXmlElement *element){
	FUNCID(Drawing::PolyLine::readXMLPrivate);

	try {
		// search for mandatory attributes
		if (!TiXmlAttribute::attributeByName(element, "id")) {
			IBK::IBK_Message( IBK::FormatString(XML_READ_ERROR).arg(element->Row()).arg(
								  IBK::FormatString("Missing required 'id' attribute.") ), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			return;
		}

		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "id")
				m_id = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "color")
				m_color = QColor(QString::fromStdString(attrib->ValueStr()));
			else if (attribName == "zPosition")
				m_zPosition = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "connected")
				m_endConnected = readPODAttributeValue<bool>(element, attrib);
			else if (attribName == "layer")
				m_layerName = QString::fromStdString(attrib->ValueStr());
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ATTRIBUTE).arg(attribName).arg(element->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			attrib = attrib->Next();
		}

		// reading elements
		// read vertexes
		std::string text = element->GetText();
		text = IBK::replace_string(text, ",", " ");
		std::vector<IBKMK::Vector2D> verts;
		try {
			std::vector<double> vals;
			IBK::string2valueVector(text, vals);
			// must have n*2 elements
			if (vals.size() % 2 != 0)
				throw IBK::Exception("Mismatching number of values.", FUNC_ID);
			if (vals.empty())
				throw IBK::Exception("Missing values.", FUNC_ID);
			verts.resize(vals.size() / 2);
			for (unsigned int i=0; i<verts.size(); ++i){
				verts[i].m_x = vals[i*2];
				verts[i].m_y = vals[i*2+1];
			}

			m_polyline = verts;

		} catch (IBK::Exception & ex) {
			throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(element->Row())
								  .arg("Error reading element 'PolyLine'." ), FUNC_ID);
		}
	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'Drawing::PolyLine' element."), FUNC_ID);
	}
	catch (std::exception & ex2) {
		throw IBK::Exception( IBK::FormatString("%1\nError reading 'Drawing::PolyLine' element.").arg(ex2.what()), FUNC_ID);
	}
}

const std::vector<IBKMK::Vector2D> &Drawing::PolyLine::points2D() const {
	if (m_dirtyLocalPoints) {
		m_pickPoints.clear();

		// iterateover data.vertlist, insert all vertices of Polyline into vector
		for(size_t i = 0; i < m_polyline.size(); ++i){
			m_pickPoints.push_back(m_polyline[i]);
		}
		m_dirtyLocalPoints = false;
	}
	return m_pickPoints;
}

const std::vector<Drawing::LineSegment> &Drawing::PolyLine::lineGeometries() const {
	//	FUNCID(Drawing::PolyLine::planeGeometries);

	if (m_dirtyGlobalPoints) {
		m_lineGeometries.clear();

		// Create Vector to store vertices of polyline
		std::vector<IBKMK::Vector3D> polylinePoints;

		const Drawing *drawing = this->drawing();
		std::vector<IBKMK::Vector3D> points3D = drawing->points3D(m_polyline, *this);

		int offset = m_endConnected ? 0 : 1;
		for (unsigned int i = 0; i < points3D.size() - offset; ++i){
			const IBKMK::Vector3D &p1 = points3D[ i		];
			const IBKMK::Vector3D &p2 = points3D[(i+1) % m_polyline.size()];
			m_lineGeometries.push_back(Drawing::LineSegment(p1, p2));
		}

		// bool success = drawing->generatePlanesFromPolyline(polylinePoints, m_endConnected,
		// 												   drawing->m_lineWeightOffset + lineWeight() * drawing->m_lineWeightScaling,
		// 												   m_lineGeometries);

		// if (!success)
		// 	return m_lineGeometries;

		m_dirtyGlobalPoints = false;
	}

	return m_lineGeometries;
}


TiXmlElement * Drawing::Arc::writeXMLPrivate(TiXmlElement * parent) const {
	if (m_id == INVALID_ID)  return nullptr;

	TiXmlElement * e = new TiXmlElement("Arc");
	parent->LinkEndChild(e);

	if (m_id != INVALID_ID)
		e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (m_color.isValid())
		e->SetAttribute("color", m_color.name().toStdString());
	if (m_zPosition != 0.0)
		e->SetAttribute("zPosition", IBK::val2string<unsigned int>(m_zPosition));
	if (!m_layerName.isEmpty())
		e->SetAttribute("layer", m_layerName.toStdString());

	TiXmlElement::appendSingleAttributeElement(e, "Center", nullptr, std::string(), m_center.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "Radius", nullptr, std::string(), IBK::val2string<double>(m_radius, PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "StartAngle", nullptr, std::string(), IBK::val2string<double>(m_startAngle, PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "EndAngle", nullptr, std::string(), IBK::val2string<double>(m_endAngle, PRECISION));

	return e;
}

void Drawing::Arc::readXMLPrivate(const TiXmlElement *element){
	FUNCID(Drawing::Arc::readXMLPrivate);

	try {
		// search for mandatory attributes
		if (!TiXmlAttribute::attributeByName(element, "id")) {
			IBK::IBK_Message( IBK::FormatString(XML_READ_ERROR).arg(element->Row()).arg(
								  IBK::FormatString("Missing required 'id' attribute.") ), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			return;
		}

		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "id")
				m_id = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "color")
				m_color = QColor(QString::fromStdString(attrib->ValueStr()));
			else if (attribName == "zPosition")
				m_zPosition = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "layer")
				m_layerName = QString::fromStdString(attrib->ValueStr());
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ATTRIBUTE).arg(attribName).arg(element->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			attrib = attrib->Next();
		}

		// reading elements
		const TiXmlElement * c = element->FirstChildElement();
		while (c) {
			const std::string & cName = c->ValueStr();
			if (cName == "Radius")
				m_radius = readPODElement<double>(c, cName);
			else if (cName == "StartAngle")
				m_startAngle = readPODElement<double>(c, cName);
			else if (cName == "EndAngle")
				m_endAngle = readPODElement<double>(c, cName);
			else if (cName == "Center") {
				try {
					m_center = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(cName).arg(c->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			c = c->NextSiblingElement();
		}
	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'Drawing::Arc' element."), FUNC_ID);
	}
	catch (std::exception & ex2) {
		throw IBK::Exception( IBK::FormatString("%1\nError reading 'Drawing::Arc' element.").arg(ex2.what()), FUNC_ID);
	}
}

const std::vector<IBKMK::Vector2D>& Drawing::Arc::points2D() const {

	if (m_dirtyLocalPoints) {
		double startAngle = m_startAngle;
		double endAngle = m_endAngle;

		double angleDifference;

		if (startAngle > endAngle)
			angleDifference = 2 * IBK::PI - startAngle + endAngle;
		else
			angleDifference = endAngle - startAngle;

		unsigned int n = std::ceil(angleDifference / (2 * IBK::PI) * SEGMENT_COUNT_ARC);
		double stepAngle = angleDifference / n;

		m_pickPoints.resize(n + 1);
		for (unsigned int i = 0; i < n+1; ++i){
			m_pickPoints[i] = IBKMK::Vector2D(m_center.m_x + m_radius * cos(startAngle + i * stepAngle),
											  m_center.m_y + m_radius * sin(startAngle + i * stepAngle));
		}

		m_dirtyLocalPoints = false;
	}
	return m_pickPoints;
}

const std::vector<Drawing::LineSegment> &Drawing::Arc::lineGeometries() const {
	//	FUNCID(Drawing::Arc::planeGeometries);


	if (m_dirtyGlobalPoints) {
		m_lineGeometries.clear();

		std::vector<IBKMK::Vector2D> points(points2D());
		for(unsigned int i = 0; i < points.size() - 1; ++i){
			const IBKMK::Vector3D &p1 = m_parent->point3D(points[  i						], *this);
			const IBKMK::Vector3D &p2 = m_parent->point3D(points[ (i+1) % SEGMENT_COUNT_ARC ], *this);

			m_lineGeometries.push_back(LineSegment(p1, p2));
		}

		m_dirtyGlobalPoints = false;
	}


	return m_lineGeometries;
}


TiXmlElement * Drawing::Ellipse::writeXMLPrivate(TiXmlElement * parent) const {
	if (m_id == INVALID_ID)  return nullptr;

	TiXmlElement * e = new TiXmlElement("Ellipse");
	parent->LinkEndChild(e);

	if (m_id != INVALID_ID)
		e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (m_color.isValid())
		e->SetAttribute("color", m_color.name().toStdString());
	if (m_zPosition != 0.0)
		e->SetAttribute("zPosition", IBK::val2string<unsigned int>(m_zPosition));
	if (!m_layerName.isEmpty())
		e->SetAttribute("layer", m_layerName.toStdString());

	TiXmlElement::appendSingleAttributeElement(e, "Center", nullptr, std::string(), m_center.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "MajorAxis", nullptr, std::string(), m_majorAxis.toString(PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "Ratio", nullptr, std::string(), IBK::val2string<double>(m_ratio));
	TiXmlElement::appendSingleAttributeElement(e, "StartAngle", nullptr, std::string(), IBK::val2string<double>(m_startAngle, PRECISION));
	TiXmlElement::appendSingleAttributeElement(e, "EndAngle", nullptr, std::string(), IBK::val2string<double>(m_endAngle, PRECISION));

	return e;
}


void Drawing::Ellipse::readXMLPrivate(const TiXmlElement *element){
	FUNCID(Drawing::Arc::readXMLPrivate);

	try {
		// search for mandatory attributes
		if (!TiXmlAttribute::attributeByName(element, "id")) {
			IBK::IBK_Message( IBK::FormatString(XML_READ_ERROR).arg(element->Row()).arg(
								  IBK::FormatString("Missing required 'id' attribute.") ), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			return;
		}

		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "id")
				m_id = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "color")
				m_color = QColor(QString::fromStdString(attrib->ValueStr()));
			else if (attribName == "zPosition")
				m_zPosition = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "layer")
				m_layerName = QString::fromStdString(attrib->ValueStr());
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ATTRIBUTE).arg(attribName).arg(element->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			attrib = attrib->Next();
		}

		// reading elements
		const TiXmlElement * c = element->FirstChildElement();
		while (c) {
			const std::string & cName = c->ValueStr();
			if (cName == "Ratio")
				m_ratio = readPODElement<double>(c, cName);
			else if (cName == "StartAngle")
				m_startAngle = readPODElement<double>(c, cName);
			else if (cName == "EndAngle")
				m_endAngle = readPODElement<double>(c, cName);
			else if (cName == "Center") {
				try {
					m_center = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else if (cName == "MajorAxis") {
				try {
					m_majorAxis = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}

			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(cName).arg(c->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			c = c->NextSiblingElement();
		}
	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'Drawing::Arc' element."), FUNC_ID);
	}
	catch (std::exception & ex2) {
		throw IBK::Exception( IBK::FormatString("%1\nError reading 'Drawing::Arc' element.").arg(ex2.what()), FUNC_ID);
	}
}

const std::vector<IBKMK::Vector2D> &Drawing::Ellipse::points2D() const {
	if (m_dirtyLocalPoints) {
		m_pickPoints.clear();

		// iterateover data.vertlist, insert all vertices of Polyline into vector
		double startAngle = m_startAngle;
		double endAngle = m_endAngle;

		double angleStep = (endAngle - startAngle) / (SEGMENT_COUNT_ELLIPSE - 1);

		double majorRadius = sqrt(pow(m_majorAxis.m_x, 2) + pow(m_majorAxis.m_y, 2));
		double minorRadius = majorRadius * m_ratio;
		double rotationAngle = atan2(m_majorAxis.m_y, m_majorAxis.m_x);

		double x, y, rotated_x, rotated_y;

		m_pickPoints.resize(SEGMENT_COUNT_ELLIPSE);

		for (unsigned int i = 0; i < SEGMENT_COUNT_ELLIPSE; ++i) {

			double currentAngle = startAngle + i * angleStep;

			x = majorRadius * cos(currentAngle);
			y = minorRadius * sin(currentAngle);

			rotated_x = x * cos(rotationAngle) - y * sin(rotationAngle);
			rotated_y = x * sin(rotationAngle) + y * cos(rotationAngle);

			m_pickPoints[i] = IBKMK::Vector2D(rotated_x + m_center.m_x,
											  rotated_y + m_center.m_y);

		}
		m_dirtyLocalPoints = false;
	}
	return m_pickPoints;
}


const std::vector<Drawing::LineSegment> &Drawing::Ellipse::lineGeometries() const {
	FUNCID(Drawing::Ellipse::planeGeometries);
	try {
		if (m_dirtyGlobalPoints) {
			m_lineGeometries.clear();

			const std::vector<IBKMK::Vector2D> &pickPoints = points2D();
			const Drawing *drawing = this->drawing();

			// ToDo Stephan: Implement line geometries
#if 0
			std::vector<IBKMK::Vector3D> ellipsePoints;
			for (unsigned int i = 0; i < SEGMENT_COUNT_ELLIPSE; i++) {

				IBKMK::Vector3D p = IBKMK::Vector3D(drawing->m_scalingFactor * pickPoints[i].m_x,
													drawing->m_scalingFactor * pickPoints[i].m_y,
													m_zPosition * Z_MULTIPLYER);

				QVector3D vec = drawing->m_rotationMatrix.toQuaternion() * IBKVector2QVector(p);
				vec += IBKVector2QVector(drawing->m_offset);

				ellipsePoints.push_back(QVector2IBKVector(vec));
			}

			Q_ASSERT(points2D().size() > 0);
#endif
			m_dirtyGlobalPoints = false;
		}

		return m_lineGeometries;

	}
	catch (IBK::Exception &ex) {
		throw IBK::Exception(ex, IBK::FormatString("Could not generate plane geometries of ellipses."), FUNC_ID);
	}
}


const Drawing::AbstractDrawingObject *Drawing::objectByID(unsigned int id) const {
	FUNCID(Drawing::objectByID);

	Q_ASSERT(m_objectPtr.find(id) != m_objectPtr.end());
	AbstractDrawingObject *obj = m_objectPtr.at(id);
	if (obj == nullptr)
		throw IBK::Exception(IBK::FormatString("Drawing Object with ID #%1 not found").arg(id), FUNC_ID);

	return obj;
}


Drawing::Block *Drawing::findBlockPointer(const QString &name, const std::map<QString, Block*> &blockRefs){
	const auto it = blockRefs.find(name);
	if (it == blockRefs.end())
		return nullptr;
	else
		return it->second;
}


const DrawingLayer* Drawing::findLayerReference(const std::map<QString, const DrawingLayer*> &layerRefs, const QString &layerName)  {
	try {
		return layerRefs.at(layerName);
	}
	catch (std::exception &ex) {
		throw IBK::Exception(ex, IBK::FormatString("Could not find layer '%1'"), "[Drawing::findLayerReference]");
	}
}


void Drawing::updatePointer(){
	FUNCID(Drawing::updatePointer);
	m_objectPtr.clear();

	if (m_drawingLayers.empty()) {
		m_drawingLayers.push_back(DrawingLayer());
		m_drawingLayers.back().m_id = 100000;
		m_drawingLayers.back().m_displayName = "0";
	}

	// map layer name to reference, this avoids nested loops
	std::map<QString, const DrawingLayer*> layerRefs;
	for (const DrawingLayer &dl: m_drawingLayers) {
		layerRefs[dl.m_displayName] = &dl;
	}


	// map block name to reference, also avoids nested loops
	std::map<QString, Block*> blockRefs;
	blockRefs[""] = nullptr; // This is just in case. But actually blocks without name should not exist
	for (Block &b: m_blocks) {
		blockRefs[b.m_name] = &b;
	}

	/* Note: Layer references must always be valid. Hence, when "layerRefs.at()" throws an exception, this is due to an invalid DXF.
	 * Block references are optional, therefore we use the access function which returns a nullptr if there is no block ref
	*/
	try {

		for (unsigned int i=0; i < m_points.size(); ++i){
			m_points[i].m_layerRef = findLayerReference(layerRefs, m_points[i].m_layerName);
			m_points[i].m_block = findBlockPointer(m_points[i].m_blockName, blockRefs);
			m_points[i].m_parent = this;
			m_objectPtr[m_points[i].m_id] = &m_points[i];
		}
		for (unsigned int i=0; i < m_lines.size(); ++i){
			m_lines[i].m_layerRef = findLayerReference(layerRefs, m_lines[i].m_layerName);
			m_lines[i].m_block = findBlockPointer(m_lines[i].m_blockName, blockRefs);
			m_lines[i].m_parent = this;
			m_objectPtr[m_lines[i].m_id] = &m_lines[i];
		}
		for (unsigned int i=0; i < m_polylines.size(); ++i){
			m_polylines[i].m_layerRef = findLayerReference(layerRefs, m_polylines[i].m_layerName);
			m_polylines[i].m_block = findBlockPointer(m_polylines[i].m_blockName, blockRefs);
			m_polylines[i].m_parent = this;
			m_objectPtr[m_polylines[i].m_id] = &m_polylines[i];
		}
		for (unsigned int i=0; i < m_circles.size(); ++i){
			m_circles[i].m_layerRef = findLayerReference(layerRefs, m_circles[i].m_layerName);
			m_circles[i].m_block = findBlockPointer(m_circles[i].m_blockName, blockRefs);
			m_circles[i].m_parent = this;
			m_objectPtr[m_circles[i].m_id] = &m_circles[i];
		}
		for (unsigned int i=0; i < m_arcs.size(); ++i){
			m_arcs[i].m_layerRef = findLayerReference(layerRefs, m_arcs[i].m_layerName);
			m_arcs[i].m_block = findBlockPointer(m_arcs[i].m_blockName, blockRefs);
			m_arcs[i].m_parent = this;
			m_objectPtr[m_arcs[i].m_id] = &m_arcs[i];
		}
		for (unsigned int i=0; i < m_ellipses.size(); ++i){
			m_ellipses[i].m_layerRef = findLayerReference(layerRefs, m_ellipses[i].m_layerName);
			m_ellipses[i].m_block = findBlockPointer(m_ellipses[i].m_blockName, blockRefs);
			m_ellipses[i].m_parent = this;
			m_objectPtr[m_ellipses[i].m_id] = &m_ellipses[i];
		}
		for (unsigned int i=0; i < m_solids.size(); ++i){
			m_solids[i].m_layerRef = findLayerReference(layerRefs, m_solids[i].m_layerName);
			m_solids[i].m_block = findBlockPointer(m_solids[i].m_blockName, blockRefs);
			m_solids[i].m_parent = this;
			m_objectPtr[m_solids[i].m_id] = &m_solids[i];
		}
		for (unsigned int i=0; i < m_texts.size(); ++i){
			m_texts[i].m_layerRef = findLayerReference(layerRefs, m_texts[i].m_layerName);
			m_texts[i].m_block = findBlockPointer(m_texts[i].m_blockName, blockRefs);
			m_texts[i].m_parent = this;
			m_objectPtr[m_texts[i].m_id] = &m_texts[i];
		}

		// For inserts there must be a valid currentBlock reference!
		for (unsigned int i=0; i < m_inserts.size(); ++i){
			m_inserts[i].m_currentBlock = findBlockPointer(m_inserts[i].m_currentBlockName, blockRefs);
			Q_ASSERT(m_inserts[i].m_currentBlock);
			m_inserts[i].m_parentBlock = findBlockPointer(m_inserts[i].m_parentBlockName, blockRefs);
		}
		for (unsigned int i=0; i < m_linearDimensions.size(); ++i){
			m_linearDimensions[i].m_layerRef = findLayerReference(layerRefs, m_linearDimensions[i].m_layerName);
			m_linearDimensions[i].m_parent = this;
			m_texts[i].m_block = findBlockPointer(m_linearDimensions[i].m_blockName, blockRefs);
			m_objectPtr[m_linearDimensions[i].m_id] = &m_linearDimensions[i];
			for(unsigned int j = 0; j < m_dimensionStyles.size(); ++j) {
				const QString &dimStyleName = m_dimensionStyles[j].m_name;
				const QString &styleName = m_linearDimensions[i].m_styleName;
				if (dimStyleName == styleName) {
					m_linearDimensions[i].m_style = &m_dimensionStyles[j];
					break;
				}
			}
			// In order to be safe
			if (m_linearDimensions[i].m_style == nullptr)
				m_linearDimensions[i].m_style = &m_dimensionStyles.front();
		}
	}
	catch (std::exception &ex) {
		throw IBK::Exception(IBK::FormatString("Error during initialization of DXF file. "
											   "Might be due to invalid layer references.\n%1").arg(ex.what()), FUNC_ID);
	}
}

template <typename t>
void updateGeometry(std::vector<t> &objects) {
	for (t &obj : objects )
		obj.updatePlaneGeometry();
}


void Drawing::updatePlaneGeometries() {
	updateGeometry<Drawing::Line>(m_lines);
	updateGeometry<Drawing::PolyLine>(m_polylines);
	updateGeometry<Drawing::Arc>(m_arcs);
	updateGeometry<Drawing::Circle>(m_circles);
	updateGeometry<Drawing::Ellipse>(m_ellipses);
	updateGeometry<Drawing::Solid>(m_solids);
	updateGeometry<Drawing::LinearDimension>(m_linearDimensions);
	updateGeometry<Drawing::Point>(m_points);
	updateGeometry<Drawing::Text>(m_texts);

	m_dirtyPickPoints = true;
}

template <typename t>
void generateObjectFromInsert(unsigned int &nextId, const Drawing::Block &block,
							  std::vector<t> &objects, const QMatrix4x4 &trans) {
	std::vector<t> newObjects;

	for (const t &obj : objects) {

		if (obj.m_block == nullptr)
			continue;

		if (obj.m_block->m_name != block.m_name)
			continue;

		t newObj(obj);
		newObj.m_id = ++nextId;
		newObj.m_trans = trans;
		newObj.m_blockName = "";
		newObj.m_block = nullptr;
		newObj.m_isInsertObject = true;

		newObjects.push_back(newObj);
	}

	objects.insert(objects.end(), newObjects.begin(), newObjects.end());
}

void Drawing::transformInsert(QMatrix4x4 trans, const Drawing::Insert &insert, unsigned int &nextId) {

	Q_ASSERT(insert.m_currentBlock != nullptr);
	IBKMK::Vector2D insertPoint = insert.m_insertionPoint - insert.m_currentBlock->m_basePoint;

	trans.translate(QVector3D(float(insertPoint.m_x),
							  float(insertPoint.m_y),
							  0.0));
	trans.rotate(float(insert.m_angle/IBK::DEG2RAD), QVector3D(0,0,1)); // Rotation is in degree
	trans.scale(float(insert.m_xScale), float(insert.m_yScale), 1);

	for (const Insert &i : m_inserts) {
		if (i.m_parentBlock == nullptr)
			continue;

		if (insert.m_currentBlockName == i.m_parentBlock->m_name) {
			transformInsert(trans, i, nextId); // we pass "trans" by value, to keep our own transformation untouched
		}
	}

	generateObjectFromInsert(nextId, *insert.m_currentBlock, m_points, trans);
	generateObjectFromInsert(nextId, *insert.m_currentBlock, m_arcs, trans);
	generateObjectFromInsert(nextId, *insert.m_currentBlock, m_circles, trans);
	generateObjectFromInsert(nextId, *insert.m_currentBlock, m_ellipses, trans);
	generateObjectFromInsert(nextId, *insert.m_currentBlock, m_lines, trans);
	generateObjectFromInsert(nextId, *insert.m_currentBlock, m_polylines, trans);
	generateObjectFromInsert(nextId, *insert.m_currentBlock, m_solids, trans);
	generateObjectFromInsert(nextId, *insert.m_currentBlock, m_texts, trans);
	generateObjectFromInsert(nextId, *insert.m_currentBlock, m_linearDimensions, trans);
}


void transformPoint(IBKMK::Vector2D &vec, const QMatrix4x4 &trans) {
	IBKMK::Vector3D v3 = QVector2IBKVector(trans * QVector3D((float)vec.m_x, (float)vec.m_y, 0));
	vec = v3.point2D();
}


void Drawing::generateInsertGeometries(unsigned int nextId) {
	FUNCID(Drawing::generateInsertGeometries);
	updateParents();

	for (const Drawing::Insert &insert : m_inserts) {

		if (insert.m_parentBlock != nullptr)
			continue;

		if (insert.m_currentBlock == nullptr)
			throw IBK::Exception(IBK::FormatString("Block with name '%1' was not found").arg(insert.m_currentBlockName.toStdString()), FUNC_ID);

		QMatrix4x4 trans;
		transformInsert(trans, insert, nextId);
	}

	updateParents();
}


void Drawing::updateAllGeometries() {
	updateGeometryForAll(m_points);
	updateGeometryForAll(m_lines);
	updateGeometryForAll(m_polylines);
	updateGeometryForAll(m_circles);
	updateGeometryForAll(m_ellipses);
	updateGeometryForAll(m_arcs);
	updateGeometryForAll(m_solids);
	updateGeometryForAll(m_texts);
	updateGeometryForAll(m_linearDimensions);
}

void Drawing::sortLayersAlphabetical() {
	std::sort(m_drawingLayers.begin(), m_drawingLayers.end(), [](const DrawingLayer& a, const DrawingLayer& b) {
		return a.m_displayName < b.m_displayName;
	});
}

template<typename t>
void addPoints(const std::vector<t> objs, const Drawing *d, std::vector<double> &xValues, std::vector<double> &yValues, int cnt) {
	int moduloThreshold = 10;
	for (const t &o : objs) {
		for (const IBKMK::Vector3D &v : d->points3D(o.points2D(), o)) {
			if(cnt % moduloThreshold == 0){
				xValues.push_back(v.m_x);
				yValues.push_back(v.m_y);
			}
			++cnt;
		}
	}
}


IBKMK::Vector3D Drawing::weightedCenterMedian(unsigned int nextId) {
	std::set<const Block *> listAllBlocksWithNoInsertPoint;
	listAllBlocksWithNoInsertPoint.insert(nullptr);
	for(Block &b : m_blocks){
		bool hasInsertBlock = false;
		for(Insert &i : m_inserts){
			if(i.m_currentBlock == &b){
				hasInsertBlock = true;
			}
		}
		if(!hasInsertBlock){
		listAllBlocksWithNoInsertPoint.insert(&b);
		}
	}

	generateInsertGeometries(nextId);

	unsigned int cnt = 0;

	std::vector<double> xValues;
	std::vector<double> yValues;

	addPoints(m_lines, this, xValues, yValues, cnt);
	addPoints(m_polylines, this, xValues, yValues, cnt);
	addPoints(m_points, this, xValues, yValues, cnt);
	addPoints(m_arcs, this, xValues, yValues, cnt);
	addPoints(m_circles, this, xValues, yValues, cnt);

	std::nth_element(xValues.begin(), xValues.begin() + xValues.size() / 2, xValues.end());
	std::nth_element(yValues.begin(), yValues.begin() + yValues.size() / 2, yValues.end());

	double x = xValues[xValues.size() / 2 - 1];
	double y = yValues[yValues.size() / 2 - 1];
	return IBKMK::Vector3D(x, y, 0);
}


const std::map<Drawing::Field, std::map<unsigned int, std::vector<IBKMK::Vector3D>>> &Drawing::pickPoints() const {
	FUNCID(Drawing::pickPoints);
	try {
		if (m_dirtyPickPoints) {

			qDebug() << "Start adding pick points.";

			m_pickPoints.clear();
			addPickPoints(m_points);
			// qDebug() << "points.";

			addPickPoints(m_arcs);
			// qDebug() << "arcs.";

			addPickPoints(m_circles);
			// qDebug() << "circles.";

			addPickPoints(m_ellipses);
			// qDebug() << "ellipses.";

			addPickPoints(m_linearDimensions, true);
			// qDebug() << "linear Dim.";

			addPickPoints(m_lines, true);
			// qDebug() << "lines.";

			addPickPoints(m_polylines, true);
			// qDebug() << "polylines.";

			addPickPoints(m_solids);
			// qDebug() << "solids.";

			// For now only line intersections are treated
			// addInstersectionPoints();

			qDebug() << "Added pick points.";

			m_dirtyPickPoints = false;
		}
		return m_pickPoints;
	}
	catch (IBK::Exception &ex) {
		throw IBK::Exception(IBK::FormatString("Could not generate pick points.\n%1").arg(ex.what()), FUNC_ID);
	}
}


const IBKMK::Vector3D Drawing::point3D(const IBKMK::Vector2D &vert, const AbstractDrawingObject &object) const {
	glm::vec3 v3D = object.transformationMatrix() * glm::dvec4(vert.m_x, vert.m_y, 0.0, 1.0);
	return IBKMK::Vector3D(v3D.x, v3D.y, v3D.z);
}


const std::vector<IBKMK::Vector3D> Drawing::points3D(const std::vector<IBKMK::Vector2D> &verts, const AbstractDrawingObject &object) const {
	std::vector<IBKMK::Vector3D> points3D(verts.size());
	for (unsigned int i=0; i < verts.size(); ++i) {
		const IBKMK::Vector2D &v2D = verts[i];
		points3D[i] = point3D(v2D, object);
	}

	return points3D;
}

const IBKMK::Vector3D Drawing::normal() const {
	return QVector2IBKVector(m_rotationMatrix.toQuaternion() * QVector3D(0,0,1));
}

const IBKMK::Vector3D Drawing::localX() const {
	return QVector2IBKVector(m_rotationMatrix.toQuaternion() * QVector3D(1,0,0));
}

const IBKMK::Vector3D Drawing::localY() const {
	return QVector2IBKVector(m_rotationMatrix.toQuaternion() * QVector3D(0,1,0));
}

void Drawing::addInstersectionPoints() const {

	// Calculate all line intersections for drawings
#if defined(_OPENMP)
#pragma omp parallel for
#endif
	for (int i = 0; i < (int)m_lines.size(); ++i) {
		const IBKMK::Vector2D &l1p1 = m_lines[i].m_point1;
		const IBKMK::Vector2D &l1p2 = m_lines[i].m_point2;

		double xL1Min = std::min(l1p1.m_x, l1p2.m_x);
		double xL1Max = std::max(l1p1.m_x, l1p2.m_x);

		double yL1Min = std::min(l1p1.m_y, l1p2.m_y);
		double yL1Max = std::max(l1p1.m_y, l1p2.m_y);

		for (int j = i; j < (int)m_lines.size(); ++j) {
			if (j == i)
				continue;

			const IBKMK::Vector2D &l2p1 = m_lines[j].m_point1;
			const IBKMK::Vector2D &l2p2 = m_lines[j].m_point2;

			double xL2Min = std::min(l2p1.m_x, l2p2.m_x);
			double xL2Max = std::max(l2p1.m_x, l2p2.m_x);

			double yL2Min = std::min(l2p1.m_y, l2p2.m_y);
			double yL2Max = std::max(l2p1.m_y, l2p2.m_y);

			// Check if bounding boxes overlap
			if (yL1Min <= yL2Max && yL2Min <= yL1Max && xL1Min <= xL2Max && xL2Min <= xL1Max) {
				// Only proceed with intersection tests if bounding boxes overlap
				IBK::Line l1 (l1p1, l1p2);
				IBK::Line l2 (l2p1, l2p2);

				if ((l1p2 - l1p1).magnitudeSquared() < 1 || (l2p2 - l2p1).magnitudeSquared() < 1)
					continue;

				IBKMK::Vector2D p1, p2;
				try {
					if (l1.intersects(l2, p1, p2) == 1) {
						IBKMK::Vector3D v1 = point3D(p1, m_lines[i]);
#if defined(_OPENMP)
#pragma omp critical
#endif
						m_pickPoints[Field(*this, v1)][m_lines[i].m_id].push_back(v1);

					}
				} catch (...) {
					continue;
				}
			}
		}
	}

	// Calculate all line intersections for drawings
#if defined(_OPENMP)
#pragma omp parallel for
#endif
	for (int i = 0; i < (int)m_polylines.size(); ++i) {

		for (unsigned int k = 0; k < m_polylines[i].m_polyline.size(); ++k) {

			const IBKMK::Vector2D &l1p1 = m_polylines[i].m_polyline[ k										  ];
			const IBKMK::Vector2D &l1p2 = m_polylines[i].m_polyline[(k + 1) % m_polylines[i].m_polyline.size()];

			double xL1Min = std::min(l1p1.m_x, l1p2.m_x);
			double xL1Max = std::max(l1p1.m_x, l1p2.m_x);

			double yL1Min = std::min(l1p1.m_y, l1p2.m_y);
			double yL1Max = std::max(l1p1.m_y, l1p2.m_y);

			for (int j = i; j < (int)m_polylines.size(); ++j) {
				if (j == i)
					continue;

				for (unsigned int l = 0; l < m_polylines[j].m_polyline.size(); ++l) {

					const IBKMK::Vector2D &l2p1 = m_polylines[j].m_polyline[ l										  ];
					const IBKMK::Vector2D &l2p2 = m_polylines[j].m_polyline[(l + 1) % m_polylines[j].m_polyline.size()];

					double xL2Min = std::min(l2p1.m_x, l2p2.m_x);
					double xL2Max = std::max(l2p1.m_x, l2p2.m_x);

					double yL2Min = std::min(l2p1.m_y, l2p2.m_y);
					double yL2Max = std::max(l2p1.m_y, l2p2.m_y);

					// Check if bounding boxes overlap
					if (yL1Min <= yL2Max && yL2Min <= yL1Max && xL1Min <= xL2Max && xL2Min <= xL1Max) {
						// Only proceed with intersection tests if bounding boxes overlap
						IBK::Line l1 (l1p1, l1p2);
						IBK::Line l2 (l2p1, l2p2);

						if ((l1p2 - l1p1).magnitudeSquared() < 1 || (l2p2 - l2p1).magnitudeSquared() < 1)
							continue;

						IBKMK::Vector2D p1, p2;
						try {
							if (l1.intersects(l2, p1, p2) == 1) {
								IBKMK::Vector3D v1 = point3D(p1, m_lines[i]);
#if defined(_OPENMP)
#pragma omp critical
#endif
								m_pickPoints[Field(*this, v1)][m_lines[i].m_id].push_back(v1);

							}
						} catch (...) {
							continue;
						}
					}
				}
			}
		}
	}
}


const DrawingLayer * Drawing::layerPointer(const QString &layername){
	for(unsigned int i = 0; i < m_drawingLayers.size(); ++i) {
		if (m_drawingLayers[i].m_displayName == layername)
			return &m_drawingLayers[i];
	}
	return nullptr;
}

const Drawing::Block *Drawing::blockPointer(const QString &name){
	for(unsigned int i = 0; i < m_blocks.size(); ++i) {
		if (m_blocks[i].m_name == name)
			return &m_blocks[i];
	}
	return nullptr;
}


bool isClockwise(const QPolygonF& polygon) {
	double sum = 0.0;
	for (int i = 0; i < polygon.count(); i++) {
		QPointF p1 = polygon[ i							];
		QPointF p2 = polygon[(i + 1) % polygon.count()]; // next point
		sum += (p2.x() - p1.x()) * (p2.y() + p1.y());
	}
	return sum > 0.0;
}


void Drawing::generateLinesFromText(const std::string &text, double textHeight, Qt::Alignment alignment,
									const double &rotationAngle, const IBKMK::Vector2D &basePoint, const AbstractDrawingObject &object,
									std::vector<LineSegment> &lineGeometries) const {

	return;
}

bool Drawing::dirtyPickPoints() const {
	return m_dirtyPickPoints;
}

const QColor & Drawing::AbstractDrawingObject::color() const{
	/* If the object has a color, return it, else use color of parent */
	if (m_color.isValid())
		return m_color;
	else if (m_layerRef != nullptr) {
		const DrawingLayer *layer = m_layerRef;
		Q_ASSERT(layer != nullptr);
		return layer->m_color;
	}

	return m_color;
}


double Drawing::AbstractDrawingObject::lineWeight() const{
	// ToDo Stephan: Improve function

	/*! if -1: use weight of layer */
	const DrawingLayer *dl = m_layerRef;

	if (dl == nullptr)
		return DEFAULT_LINE_WEIGHT;

	if (m_lineWeight <= 0) {
		if (dl->m_lineWeight < 0)
			return DEFAULT_LINE_WEIGHT;
		return dl->m_lineWeight;
	}
	else
		return m_lineWeight;
}

const glm::dmat4 &Drawing::AbstractDrawingObject::transformationMatrix() const {
	if (m_dirtyGlobalPoints) {
		Q_ASSERT(m_parent != nullptr);
		double zCoordinate = m_zPosition * Z_MULTIPLYER;

		// Convert QMatrix4x4 to glm::mat4
		glm::dmat4 insertMatrix;
		const float *data = m_trans.constData();
		for (int i = 0; i < 16; ++i) {
			insertMatrix[i / 4][i % 4] = data[i];
		}

		// Vektoren für Transformationen definieren
		glm::dvec3 translationVector(m_parent->m_offset.m_x, m_parent->m_offset.m_y, m_parent->m_offset.m_z); // Translation um 1 Einheit auf x, 2 auf y, 3 auf z
		translationVector += glm::dvec3(0,0,zCoordinate);
		glm::dvec3 scaleVector(m_parent->m_scalingFactor, m_parent->m_scalingFactor, 1.0);			// Skalierung um Faktor 2 in alle Richtungen

		// Winkel für die Rotation
		// Create a GLM quaternion
		glm::quat glmQuat((float)m_parent->m_rotationMatrix.m_wp,
						  (float)m_parent->m_rotationMatrix.m_x,
						  (float)m_parent->m_rotationMatrix.m_y,
						  (float)m_parent->m_rotationMatrix.m_z);

		// Convert GLM quaternion to a rotation matrix
		glm::dmat4 rotationMatrix = glm::toMat4(glmQuat);

		// Erzeugung der einzelnen Transformationsmatrizen
		glm::dmat4 identityMatrix = glm::mat4(1.0f); // Einheitsmatrix
		glm::dmat4 translationMatrix = glm::translate(identityMatrix, translationVector);
		glm::dmat4 scaleMatrix = glm::scale(identityMatrix, scaleVector);

		// Kombinieren der Transformationen
		m_transformationMatrix = translationMatrix * rotationMatrix * scaleMatrix * insertMatrix;
	}

	return m_transformationMatrix;
}



// *** XML Read/Write



TiXmlElement *Drawing::DimStyle::writeXML(TiXmlElement *parent) const {
	if (m_id == INVALID_ID)  return nullptr;

	TiXmlElement * e = new TiXmlElement("DimStyle");
	parent->LinkEndChild(e);

	if (m_id != INVALID_ID)
		e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_name.isEmpty())
		e->SetAttribute("name", m_name.toStdString());
	if (m_upperLineDistance > 0.0)
		e->SetAttribute("upperLineDistance", IBK::val2string<double>(m_upperLineDistance));
	if (m_extensionLineLowerDistance > 0.0)
		e->SetAttribute("extensionLineLowerDistance", IBK::val2string<double>(m_extensionLineLowerDistance));
	if (m_extensionLineLength > 0.0)
		e->SetAttribute("extensionLineLength", IBK::val2string<double>(m_extensionLineLength));
	if (!m_fixedExtensionLength)
		e->SetAttribute("fixedExtensionLength", IBK::val2string<bool>(m_fixedExtensionLength));
	if (m_textHeight > 0.0)
		e->SetAttribute("textHeight", IBK::val2string<double>(m_textHeight));
	if (m_globalScalingFactor != 1.0)
		e->SetAttribute("globalScalingFactor", IBK::val2string<double>(m_globalScalingFactor));
	if (m_globalScalingFactor != 1.0)
		e->SetAttribute("textScalingFactor", IBK::val2string<double>(m_textScalingFactor));
	if (m_textLinearFactor != 1.0)
		e->SetAttribute("textLinearFactor", IBK::val2string<double>(m_textLinearFactor));
	if (m_textDecimalPlaces != 1.0)
		e->SetAttribute("textDecimalPlaces", IBK::val2string<int>(m_textDecimalPlaces));

	return e;
}

void Drawing::DimStyle::readXML(const TiXmlElement *element) {
	FUNCID(Drawing::DimStyle::readXMLPrivate);

	try {
		// search for mandatory attributes
		if (!TiXmlAttribute::attributeByName(element, "id")) {
			IBK::IBK_Message( IBK::FormatString(XML_READ_ERROR).arg(element->Row()).arg(
								  IBK::FormatString("Missing required 'id' attribute.") ), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			return;
		}


		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "id")
				m_id = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "name")
				m_name = QString::fromStdString(attrib->ValueStr());
			else if (attribName == "upperLineDistance")
				m_upperLineDistance = readPODAttributeValue<double>(element, attrib);
			else if (attribName == "extensionLineLowerDistance")
				m_extensionLineLowerDistance = readPODAttributeValue<double>(element, attrib);
			else if (attribName == "extensionLineLength")
				m_extensionLineLength = readPODAttributeValue<double>(element, attrib);
			else if (attribName == "fixedExtensionLength")
				m_fixedExtensionLength = readPODAttributeValue<bool>(element, attrib);
			else if (attribName == "textHeight")
				m_textHeight = readPODAttributeValue<double>(element, attrib);
			else if (attribName == "globalScalingFactor")
				m_globalScalingFactor = readPODAttributeValue<double>(element, attrib);
			else if (attribName == "textLinearFactor")
				m_textLinearFactor = readPODAttributeValue<double>(element, attrib);
			else if (attribName == "textDecimalPlaces")
				m_textDecimalPlaces = readPODAttributeValue<int>(element, attrib);

			attrib = attrib->Next();
		}

		// reading elements

	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'Drawing::DimStyle' element."), FUNC_ID);
	}
	catch (std::exception & ex2) {
		throw IBK::Exception( IBK::FormatString("%1\nError reading 'Drawing::DimStyle' element.").arg(ex2.what()), FUNC_ID);
	}
}


TiXmlElement * Drawing::Block::writeXML(TiXmlElement * parent) const {
	if (m_id == INVALID_ID)  return nullptr;

	TiXmlElement * e = new TiXmlElement("Block");
	parent->LinkEndChild(e);

	if (m_id != INVALID_ID)
		e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (m_color.isValid())
		e->SetAttribute("color", m_color.name().toStdString());
	if (!m_name.isEmpty())
		e->SetAttribute("name", m_name.toStdString());
	if (m_lineWeight > 0)
		e->SetAttribute("lineWeight", IBK::val2string<int>(m_lineWeight));

	TiXmlElement::appendSingleAttributeElement(e, "basePoint", nullptr, std::string(), m_basePoint.toString(PRECISION));

	return e;
}


void Drawing::Block::readXML(const TiXmlElement *element){
	FUNCID(Drawing::Circle::readXMLPrivate);

	try {
		// search for mandatory attributes
		if (!TiXmlAttribute::attributeByName(element, "id")) {
			IBK::IBK_Message( IBK::FormatString(XML_READ_ERROR).arg(element->Row()).arg(
								  IBK::FormatString("Missing required 'id' attribute.") ), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			return;
		}

		// reading attributes
		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "id")
				m_id = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "lineWeight")
				m_lineWeight = readPODAttributeValue<int>(element, attrib);
			else if (attribName == "name")
				m_name = QString::fromStdString(attrib->ValueStr());
			else if (attribName == "color")
				m_color.setNamedColor(QString::fromStdString(attrib->ValueStr()));
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ATTRIBUTE).arg(attribName).arg(element->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			attrib = attrib->Next();
		}

		// reading elements
		const TiXmlElement * c = element->FirstChildElement();
		while (c) {
			const std::string & cName = c->ValueStr();
			if (cName == "basePoint") {
				try {
					m_basePoint = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(cName).arg(c->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			c = c->NextSiblingElement();
		}
	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'ZoneTemplate' element."), FUNC_ID);
	}
	catch (std::exception & ex2) {
		throw IBK::Exception( IBK::FormatString("%1\nError reading 'ZoneTemplate' element.").arg(ex2.what()), FUNC_ID);
	}
}


TiXmlElement *Drawing::Insert::writeXML(TiXmlElement *parent) const {
	TiXmlElement * e = new TiXmlElement("Insert");
	parent->LinkEndChild(e);

	if (!m_currentBlockName.isEmpty())
		e->SetAttribute("blockName", m_currentBlockName.toStdString());
	if (!m_parentBlockName.isEmpty())
		e->SetAttribute("parentBlockName", m_parentBlockName.toStdString());
	if (m_angle != 0.0)
		e->SetAttribute("angle", IBK::val2string<double>(m_angle));
	if (m_xScale != 1.0)
		e->SetAttribute("xScale", IBK::val2string<double>(m_xScale));
	if (m_yScale != 1.0)
		e->SetAttribute("yScale", IBK::val2string<double>(m_yScale));
	if (m_zScale != 1.0)
		e->SetAttribute("zScale", IBK::val2string<double>(m_zScale));

	TiXmlElement::appendSingleAttributeElement(e, "insertionPoint", nullptr, std::string(), m_insertionPoint.toString(PRECISION));

	return e;
}


void Drawing::Insert::readXML(const TiXmlElement *element) {
	FUNCID(Drawing::DimStyle::readXMLPrivate);

	try {
		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "blockName")
				m_currentBlockName = QString::fromStdString(attrib->ValueStr());
			else if (attribName == "parentBlockName")
				m_parentBlockName = QString::fromStdString(attrib->ValueStr());
			else if (attribName == "angle")
				m_angle= readPODAttributeValue<double>(element, attrib);
			else if (attribName == "xScale")
				m_xScale = readPODAttributeValue<double>(element, attrib);
			else if (attribName == "yScale")
				m_yScale = readPODAttributeValue<double>(element, attrib);
			else if (attribName == "zScale")
				m_zScale = readPODAttributeValue<double>(element, attrib);

			attrib = attrib->Next();
		}

		// reading elements
		const TiXmlElement * c = element->FirstChildElement();
		while (c) {
			const std::string & cName = c->ValueStr();
			if (cName == "insertionPoint") {
				try {
					m_insertionPoint = IBKMK::Vector2D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(cName).arg(c->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			c = c->NextSiblingElement();
		}

	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'Drawing::Insert' element."), FUNC_ID);
	}
	catch (std::exception & ex2) {
		throw IBK::Exception( IBK::FormatString("%1\nError reading 'Drawing::Insert' element.").arg(ex2.what()), FUNC_ID);
	}
}


void Drawing::readXML(const TiXmlElement * element) {
	FUNCID(Drawing::readXML);

	try {
		// search for mandatory attributes
		if (!TiXmlAttribute::attributeByName(element, "id")) {
			IBK::IBK_Message( IBK::FormatString(XML_READ_ERROR).arg(element->Row()).arg(
								  IBK::FormatString("Missing required 'id' attribute.") ), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			return;
		}

		// reading attributes
		const TiXmlAttribute * attrib = element->FirstAttribute();
		while (attrib) {
			const std::string & attribName = attrib->NameStr();
			if (attribName == "id")
				m_id = readPODAttributeValue<unsigned int>(element, attrib);
			else if (attribName == "displayName")
				m_displayName = QString::fromStdString(attrib->ValueStr());
			else if (attribName == "visible")
				m_visible = readPODAttributeValue<bool>(element, attrib);
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ATTRIBUTE).arg(attribName).arg(element->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			attrib = attrib->Next();
		}
		// search for mandatory elements
		// reading elements
		const TiXmlElement * c = element->FirstChildElement();
		while (c) {
			const std::string & cName = c->ValueStr();
			if (cName == "Origin") {
				try {
					m_offset = IBKMK::Vector3D::fromString(c->GetText());
				} catch (IBK::Exception & ex) {
					throw IBK::Exception( ex, IBK::FormatString(XML_READ_ERROR).arg(c->Row())
										  .arg("Invalid vector data."), FUNC_ID);
				}
			}
			else if (cName == "ScalingFactor")
				m_scalingFactor = readPODElement<double>(c, cName);
			else if (cName == "LineWeightScaling")
				m_lineWeightScaling = readPODElement<double>(c, cName);
			else if (cName == "Blocks") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "Block")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					Drawing::Block obj;
					obj.readXML(c2);
					m_blocks.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "DrawingLayers") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "DrawingLayer")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					DrawingLayer obj;
					obj.readXML(c2);
					m_drawingLayers.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "Points") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "Point")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					Point obj;
					obj.readXML(c2);
					m_points.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "Lines") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "Line")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					Line obj;
					obj.readXML(c2);
					m_lines.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "Polylines") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "PolyLine")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					PolyLine obj;
					obj.readXML(c2);
					m_polylines.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "Circles") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "Circle")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					Circle obj;
					obj.readXML(c2);
					m_circles.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "Ellipses") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "Ellipse")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					Ellipse obj;
					obj.readXML(c2);
					m_ellipses.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "Arcs") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "Arc")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					Arc obj;
					obj.readXML(c2);
					m_arcs.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "Solids") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "Solid")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					Solid obj;
					obj.readXML(c2);
					m_solids.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "Texts") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "Text")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					Text obj;
					obj.readXML(c2);
					m_texts.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "LinearDimensions") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "LinearDimension")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					LinearDimension obj;
					obj.readXML(c2);
					m_linearDimensions.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "DimensionStyles") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "DimStyle")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					DimStyle obj;
					obj.readXML(c2);
					m_dimensionStyles.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "Inserts") {
				const TiXmlElement * c2 = c->FirstChildElement();
				while (c2) {
					const std::string & c2Name = c2->ValueStr();
					if (c2Name != "Insert")
						IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(c2Name).arg(c2->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
					Insert obj;
					obj.readXML(c2);
					m_inserts.push_back(obj);
					c2 = c2->NextSiblingElement();
				}
			}
			else if (cName == "ZCounter")
				m_zCounter = readPODElement<unsigned int>(c, cName);
			else if (cName == "DefaultColor")
				m_defaultColor.setNamedColor(QString::fromStdString(c->GetText()));
			// else if (cName == "RotationMatrix")
			// 	m_rotationMatrix.readXML(c);
			else {
				IBK::IBK_Message(IBK::FormatString(XML_READ_UNKNOWN_ELEMENT).arg(cName).arg(c->Row()), IBK::MSG_WARNING, FUNC_ID, IBK::VL_STANDARD);
			}
			c = c->NextSiblingElement();
		}
	}
	catch (IBK::Exception & ex) {
		throw IBK::Exception( ex, IBK::FormatString("Error reading 'Drawing' element."), FUNC_ID);
	}
	catch (std::exception & ex2) {
		throw IBK::Exception( IBK::FormatString("%1\nError reading 'Drawing::Text' element.").arg(ex2.what()), FUNC_ID);
	}
}


TiXmlElement * Drawing::writeXML(TiXmlElement * parent) const {
	if (m_id == INVALID_ID)  return nullptr;
	TiXmlElement * e = new TiXmlElement("Drawing");
	parent->LinkEndChild(e);

	if (m_id != INVALID_ID)
		e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_displayName.isEmpty())
		e->SetAttribute("displayName", m_displayName.toStdString());
	if (m_visible != Drawing().m_visible)
		e->SetAttribute("visible", IBK::val2string<bool>(m_visible));
	qDebug() << "Point: " << QString::fromStdString(m_offset.toString(16));
	TiXmlElement::appendSingleAttributeElement(e, "Origin", nullptr, std::string(), m_offset.toString(16));
	m_rotationMatrix.writeXML(e);
	TiXmlElement::appendSingleAttributeElement(e, "ScalingFactor", nullptr, std::string(), IBK::val2string<double>(m_scalingFactor));
	TiXmlElement::appendSingleAttributeElement(e, "LineWeightScaling", nullptr, std::string(), IBK::val2string<double>(m_lineWeightScaling));

	if (!m_blocks.empty()) {
		TiXmlElement * child = new TiXmlElement("Blocks");
		e->LinkEndChild(child);

		for (std::vector<Drawing::Block>::const_iterator it = m_blocks.begin();
			 it != m_blocks.end(); ++it)
		{
			it->writeXML(child);
		}
	}

	if (!m_drawingLayers.empty()) {
		TiXmlElement * child = new TiXmlElement("DrawingLayers");
		e->LinkEndChild(child);

		for (std::vector<DrawingLayer>::const_iterator it = m_drawingLayers.begin();
			 it != m_drawingLayers.end(); ++it)
		{
			it->writeXML(child);
		}
	}

	if (!m_points.empty()) {
		TiXmlElement * child = new TiXmlElement("Points");
		e->LinkEndChild(child);

		for (std::vector<Point>::const_iterator it = m_points.begin();
			 it != m_points.end(); ++it)
		{
			it->writeXML(child);
		}
	}

	if (!m_lines.empty()) {
		TiXmlElement * child = new TiXmlElement("Lines");
		e->LinkEndChild(child);

		for (std::vector<Line>::const_iterator it = m_lines.begin();
			 it != m_lines.end(); ++it)
		{
			it->writeXML(child);
		}
	}

	if (!m_polylines.empty()) {
		TiXmlElement * child = new TiXmlElement("Polylines");
		e->LinkEndChild(child);

		for (std::vector<PolyLine>::const_iterator it = m_polylines.begin();
			 it != m_polylines.end(); ++it)
		{
			it->writeXML(child);
		}
	}

	if (!m_circles.empty()) {
		TiXmlElement * child = new TiXmlElement("Circles");
		e->LinkEndChild(child);

		for (std::vector<Circle>::const_iterator it = m_circles.begin();
			 it != m_circles.end(); ++it)
		{
			it->writeXML(child);
		}
	}

	if (!m_ellipses.empty()) {
		TiXmlElement * child = new TiXmlElement("Ellipses");
		e->LinkEndChild(child);

		for (std::vector<Ellipse>::const_iterator it = m_ellipses.begin();
			 it != m_ellipses.end(); ++it)
		{
			it->writeXML(child);
		}
	}

	if (!m_arcs.empty()) {
		TiXmlElement * child = new TiXmlElement("Arcs");
		e->LinkEndChild(child);

		for (std::vector<Arc>::const_iterator it = m_arcs.begin();
			 it != m_arcs.end(); ++it)
		{
			it->writeXML(child);
		}
	}

	if (!m_solids.empty()) {
		TiXmlElement * child = new TiXmlElement("Solids");
		e->LinkEndChild(child);

		for (std::vector<Solid>::const_iterator it = m_solids.begin();
			 it != m_solids.end(); ++it)
		{
			it->writeXML(child);
		}
	}

	if (!m_texts.empty()) {
		TiXmlElement * child = new TiXmlElement("Texts");
		e->LinkEndChild(child);

		for (std::vector<Text>::const_iterator it = m_texts.begin();
			 it != m_texts.end(); ++it)
		{
			it->writeXML(child);
		}
	}

	if (!m_linearDimensions.empty()) {
		TiXmlElement * child = new TiXmlElement("LinearDimensions");
		e->LinkEndChild(child);

		for (std::vector<LinearDimension>::const_iterator it = m_linearDimensions.begin();
			 it != m_linearDimensions.end(); ++it)
		{
			it->writeXML(child);
		}
	}

	if (!m_dimensionStyles.empty()) {
		TiXmlElement * child = new TiXmlElement("DimensionStyles");
		e->LinkEndChild(child);

		for (std::vector<DimStyle>::const_iterator it = m_dimensionStyles.begin();
			 it != m_dimensionStyles.end(); ++it)
		{
			it->writeXML(child);
		}
	}


	if (!m_inserts.empty()) {
		TiXmlElement * child = new TiXmlElement("Inserts");
		e->LinkEndChild(child);

		for (std::vector<Insert>::const_iterator it = m_inserts.begin();
			 it != m_inserts.end(); ++it)
		{
			it->writeXML(child);
		}
	}

	if (m_zCounter != INVALID_ID)
		TiXmlElement::appendSingleAttributeElement(e, "ZCounter", nullptr, std::string(), IBK::val2string<unsigned int>(m_zCounter));
	if (m_defaultColor.isValid())
		TiXmlElement::appendSingleAttributeElement(e, "DefaultColor", nullptr, std::string(), m_defaultColor.name().toStdString());
	return e;
}
