#include "ImportDXFDialog.h"
#include "ui_ImportDXFDialog.h"

#include <QMessageBox>
#include <QFileInfo>
#include <QTimer>

#include <cmath>
#include <limits>
#include <regex>

#include <IBK_physics.h>
#include <IBK_messages.h>

#include <IBKMK_3DCalculations.h>


ImportDXFDialog::ImportDXFDialog(QWidget *parent) :
	QDialog(parent),
	m_ui(new Ui::ImportDXFDialog)
{
	m_ui->setupUi(this);

	resize(800, 600);

	setWindowTitle(tr("Import DXF Drawing"));

	QString defaultName = tr("Drawing");

	m_ui->lineEditDrawingName->setText(defaultName);

	m_ui->comboBoxUnit->addItem(tr("Auto"), SU_Auto);
	m_ui->comboBoxUnit->addItem(tr("Meter"), SU_Meter);
	m_ui->comboBoxUnit->addItem(tr("Decimeter"), SU_Decimeter);
	m_ui->comboBoxUnit->addItem(tr("Centimeter"), SU_Centimeter);
	m_ui->comboBoxUnit->addItem(tr("Millimeter"), SU_Millimeter);

	// we start with simple mode
	m_ui->checkBoxShowDetails->setChecked(false);
	m_detailedMode = false;

	m_ui->progressBar->setValue(0);
	m_ui->progressBar->update();
	m_ui->progressBar->setEnabled(false);

	m_ui->checkBoxCustomOrigin->setChecked(false);

	m_ui->lineEditCustomCenterX->setup(std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(),
									   tr("Custom center x coordinate"));
	m_ui->lineEditCustomCenterY->setup(std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(),
									   tr("Custom center y coordinate"));

	m_ui->checkBoxGeoreference->setChecked(false);
	updateGeoreferenceControls();
}

ImportDXFDialog::~ImportDXFDialog() {
	delete m_ui;
}


ImportDXFDialog::ImportResults ImportDXFDialog::importFile(const QString &fname) {

	if (m_ui->lineEditDrawingName->text().trimmed().isEmpty()) {
		QMessageBox::critical(this, QString(), tr("Please enter a descriptive name!"));
		m_ui->lineEditDrawingName->selectAll();
		m_ui->lineEditDrawingName->setFocus();
	}

	if (m_detailedMode)
		m_ui->pushButtonImport->setEnabled(false);

	m_filePath = fname;

	QFileInfo finfo(fname);
	m_ui->lineEditDrawingName->setText(finfo.fileName());

	detectGeoreferencing();

	int res = exec();
	if (res == QDialog::Rejected)
		return ImportCancelled;

	// a georeferenced drawing is placed absolutely, the centering options must not touch it
	if (!m_georeferenced && m_ui->checkBoxMove->isChecked()) {
		// set custom origin ?
		if (m_ui->checkBoxCustomOrigin->isChecked())
			m_drawing.m_offset = IBKMK::Vector3D(m_ui->lineEditCustomCenterX->value(),
												 m_ui->lineEditCustomCenterY->value(),
												 0);
		// move to origin
		// m_drawing.moveToOrigin();
	}

	//	if (m_ui->checkBoxFixFonts->isChecked())
	fixFonts();

	m_ui->plainTextEditLogWindow->clear();

	return m_returnCode;
}


void ImportDXFDialog::on_pushButtonConvert_clicked() {
	FUNCID(ImportDXFDialog::on_pushButtonConvert_clicked);

	setEnabled(false);
	m_ui->progressBar->setEnabled(true);
	m_ui->progressBar->setRange(0,4);
	m_ui->progressBar->setFormat("Reading file %p%");
	m_ui->progressBar->setValue(1);
	m_ui->progressBar->setTextVisible(true);
	m_ui->progressBar->update();

	QString log;
	QFile fileName(m_filePath);
	if (!fileName.exists()) {
		throw QMessageBox::warning(this, tr("DXF Conversion"), tr("File %1 does not exist.").arg(fileName.fileName()));
		log += "File " + fileName.fileName() + " does not exist! Aborting Conversion.\n";
	}

	bool success;
	try {
		// we clear the drawing
		m_drawing = Drawing();
		m_drawing.m_id = 1;
		m_nextId = 3;

		success = readDxfFile(m_drawing, fileName.fileName());

		m_ui->progressBar->setValue(2);
		m_ui->progressBar->setFormat("Update References %p%");

		// we need to generate inserted geometries here only in order to find the correct drawing center!

		m_drawing.sortLayersAlphabetical();
		m_drawing.updateParents();

		if (!m_ui->checkBoxImportText->isChecked()) {
			m_drawing.m_texts.clear();
			m_drawing.m_linearDimensions.clear();
		}

		if (!success)
			throw IBK::Exception(IBK::FormatString("Import of DXF-File was not successful!"), FUNC_ID);

		// set name for drawing from lineEdit
		m_drawing.m_displayName = m_ui->lineEditDrawingName->text();

		log += "Import successful!\nThe following objects were imported:\n";
		log += QString("---------------------------------------------------------\n");
		log += QString("Layers:\t\t%1\n").arg(m_drawing.m_drawingLayers.size());
		log += QString("Lines:\t\t%1\n").arg(m_drawing.m_lines.size());
		log += QString("Polylines:\t\t%1\n").arg(m_drawing.m_polylines.size());
		log += QString("Arcs:\t\t%1\n").arg(m_drawing.m_arcs.size());
		log += QString("Circles:\t\t%1\n").arg(m_drawing.m_circles.size());
		log += QString("Ellipses:\t\t%1\n").arg(m_drawing.m_ellipses.size());
		log += QString("Points:\t\t%1\n").arg(m_drawing.m_points.size());
		log += QString("Linear Dimensions:\t%1\n").arg(m_drawing.m_linearDimensions.size());
		log += QString("Dimension Styles:\t%1\n").arg(m_drawing.m_dimensionStyles.size());
		log += QString("Inserts:\t\t%1\n").arg(m_drawing.m_inserts.size());
		log += QString("Solids:\t\t%1\n").arg(m_drawing.m_solids.size());
		log += QString("---------------------------------------------------------\n");

		ScaleUnit su = (ScaleUnit)m_ui->comboBoxUnit->currentData().toInt();

		double scalingFactor[NUM_SU] = {0.001, 1.0, 0.1, 0.01, 0.001};

		std::map<ScaleUnit, std::string> unit {
			{SU_Meter,  "Meter"},
			{SU_Centimeter,  "Centimeter"},
			{SU_Decimeter,  "Decimeter"},
			{SU_Millimeter,  "Millimeter"},
		};

		m_ui->progressBar->setValue(3);
		m_ui->progressBar->setFormat("Calculate bounding box and center %p%");

		IBKMK::Vector3D dummy;
		m_drawing.updatePointer();
		IBKMK::Vector3D bounding = boundingBox(&m_drawing, dummy, false, 1.0);

		// compensate coordinates
		// m_drawing.compensateCoordinates();

		// calculate center - a georeferenced drawing gets its offset from the coordinate reference system
		if (!m_ui->checkBoxGeoreference->isChecked() && m_drawing.m_offset == IBKMK::Vector3D()) {
			IBKMK::Vector3D center = m_drawing.weightedCenterMedian(m_nextId);
			m_drawing.m_offset = -1.0 * center;
		}

		// Drawing should be at least bigger than 150 m
		double AUTO_SCALING_MIN_THRESHOLD =	  800;
		double AUTO_SCALING_MAX_THRESHOLD =  2000;
		std::string foundUnit;
		if (su == SU_Auto) {
			bool foundAutoScaling = false;
			for (unsigned int i=1; i<NUM_SU; ++i) { // skip auto scaling

				if (scalingFactor[i] * bounding.m_x > AUTO_SCALING_MIN_THRESHOLD)
					continue;

				if (scalingFactor[i] * bounding.m_y > AUTO_SCALING_MIN_THRESHOLD)
					continue;

				scalingFactor[SU_Auto] = scalingFactor[i];
				foundUnit = unit[(ScaleUnit)i];
				foundAutoScaling = true;
				break;
			}

			if (!foundAutoScaling) {
				for (unsigned int i=SU_Millimeter; i>0; --i) {

					if (scalingFactor[i] * bounding.m_x < AUTO_SCALING_MAX_THRESHOLD)
						continue;

					if (scalingFactor[i] * bounding.m_y < AUTO_SCALING_MAX_THRESHOLD)
						continue;

					scalingFactor[SU_Auto] = scalingFactor[i];
					foundUnit = unit[(ScaleUnit)i];
					foundAutoScaling = true;
					break;
				}
			}

			if (foundAutoScaling) {
				log += QString("Found auto scaling unit: %1 m\n").arg(scalingFactor[SU_Auto]);
				if (!IBK::near_equal(scalingFactor[SU_Auto], m_dxfScalingFactor)) {
					log += QString("Scaling factor from header does not match auto-determined scale factor.\n");

					// Create a message box
					QMessageBox msgBox(this);
					msgBox.setWindowTitle(tr("Choose scaling factor"));
					msgBox.setText(tr("Scaling factor from header does not match auto-determined "
									  "scale factor.\nChoose the scaling factor to use:"));

					IBKMK::Vector3D boundingDxf = boundingBox(&m_drawing, dummy, false, m_dxfScalingFactor);
					IBKMK::Vector3D boundingAuto = boundingBox(&m_drawing, dummy, false, scalingFactor[SU_Auto]);

					// Add two buttons with different scaling factors
					QPushButton *button2 = msgBox.addButton(tr("Auto-determinded: %1\n(%2 to Meters)\nWidht: %3 m\nHeight: %4 m")
															.arg(scalingFactor[SU_Auto])
															.arg(QString::fromStdString(foundUnit))
															.arg(boundingAuto.m_x, 0, 'f', 2)
															.arg(boundingAuto.m_y, 0, 'f', 2), QMessageBox::AcceptRole);
					QPushButton *button1 = msgBox.addButton(tr("DXF: %1\n(%2 to Meters)\nWidht: %3 m\nHeight: %4 m")
															.arg(m_dxfScalingFactor)
															.arg(QString::fromStdString(m_dxfScalingUnit))
															.arg(boundingDxf.m_x, 0, 'f', 2)
															.arg(boundingDxf.m_y, 0, 'f', 2), QMessageBox::AcceptRole);

					// msgBox.setFixedWidth(1500);
					// Show the message box and wait for user input
					msgBox.exec();

					// Determine which button was clicked
					if (msgBox.clickedButton() == button1)
						m_drawing.m_scalingFactor = m_dxfScalingFactor;
					else if (msgBox.clickedButton() == button2)
						m_drawing.m_scalingFactor = scalingFactor[SU_Auto];

					qDebug() << "Current scaling factor is: " << m_drawing.m_scalingFactor;
				}
			}
			else
				log += QString("Could not find auto scaling unit. Taking: %1 m\n").arg(scalingFactor[SU_Auto]);
		}
		else
			m_drawing.m_scalingFactor = scalingFactor[su];

		log += QString("Current dimensions - X: %1 Y: %2 Z: %3\n").arg(scalingFactor[su] * bounding.m_x)
				.arg(scalingFactor[su] * bounding.m_y)
				.arg(scalingFactor[su] * bounding.m_z);

		log += QString("Current center - X: %1 Y: %2 Z: %3\n")
				.arg(scalingFactor[su] * m_drawing.m_offset.m_x,
					 scalingFactor[su] * m_drawing.m_offset.m_y,
					 scalingFactor[su] * m_drawing.m_offset.m_z);
		log += QString("---------------------------------------------------------\n");
		log += QString("\nPLEASE MIND: Currently are no hatchings supported.\n");

		m_georeferenced = false;
		if (m_ui->checkBoxGeoreference->isChecked())
			m_georeferenced = applyGeoreferencing(log);

		// the georeferenced offset is already absolute, only the centering offset is in drawing units
		if (!m_georeferenced)
			m_drawing.m_offset *= m_drawing.m_scalingFactor;

	} catch (IBK::Exception &ex) {

		log += "Error in converting DXF-File. See Error below\n";
		log += QString::fromStdString(ex.msgStack());

		QMessageBox messageBox(this);
		messageBox.setIcon(QMessageBox::Critical);
		messageBox.setText(tr("Could not import DXF file."));
		std::string msg = ex.msgStack();
		messageBox.setDetailedText(QString("%1").arg(QString::fromStdString(msg)));
		messageBox.exec();

		setEnabled(true);
		bool lineEditsEnabled = m_ui->checkBoxCustomOrigin->isChecked();
		m_ui->lineEditCustomCenterX->setEnabled(lineEditsEnabled);
		m_ui->lineEditCustomCenterY->setEnabled(lineEditsEnabled);
		return;
	}

	m_ui->plainTextEditLogWindow->setPlainText(log);

	m_ui->progressBar->setFormat("Finished %p%");
	m_ui->progressBar->setValue(4);

	m_ui->pushButtonImport->setEnabled(success);
	QMessageBox::information(this, tr("DXF-Import"), tr("DXF import successful. If the scaling factor is not set correctly, "
														"you can adjust it by double-clicking the DXF node in the left navigation tree."));
	setEnabled(true);
	bool lineEditsEnabled = m_ui->checkBoxCustomOrigin->isChecked();
	m_ui->lineEditCustomCenterX->setEnabled(lineEditsEnabled);
	m_ui->lineEditCustomCenterY->setEnabled(lineEditsEnabled);
}


void ImportDXFDialog::on_pushButtonImport_clicked() {
	if (!m_detailedMode)
		on_pushButtonConvert_clicked();
	m_returnCode = AddDrawings;
	accept();
}

void ImportDXFDialog::on_lineEditCustomCenterX_editingFinished() {
	updateImportButtonEnabledState();
}


void ImportDXFDialog::on_lineEditCustomCenterY_editingFinished() {
	updateImportButtonEnabledState();
}


void ImportDXFDialog::on_groupBoxCustomCenter_clicked() {
	updateImportButtonEnabledState();
}


void ImportDXFDialog::on_checkBoxMove_clicked(bool checked) {
	m_ui->checkBoxCustomOrigin->setEnabled(checked);
	if (!checked)
		m_ui->checkBoxCustomOrigin->setChecked(false);
	on_checkBoxCustomOrigin_stateChanged(checked);
}

void ImportDXFDialog::on_checkBoxCustomOrigin_stateChanged(int arg1) {
	m_ui->labelX->setEnabled(arg1);
	m_ui->labelY->setEnabled(arg1);
	m_ui->lineEditCustomCenterX->setEnabled(arg1);
	m_ui->lineEditCustomCenterY->setEnabled(arg1);
}

void ImportDXFDialog::setProjectWorldOrigin(const IBKMK::Vector3D & origin, int utmZone, bool north) {
	m_worldOrigin = origin;
	m_worldUtmZone = utmZone;
	m_worldNorth = north;
	m_haveProjectOrigin = true;
}


void ImportDXFDialog::on_checkBoxGeoreference_toggled(bool /*checked*/) {
	updateGeoreferenceControls();
}


void ImportDXFDialog::updateGeoreferenceControls() {
	bool georeference = m_ui->checkBoxGeoreference->isChecked();

	m_ui->labelCRS->setEnabled(georeference);
	m_ui->lineEditCRS->setEnabled(georeference);

	// georeferencing dictates the placement, the centering options would fight it
	m_ui->checkBoxMove->setEnabled(!georeference);
	m_ui->checkBoxCustomOrigin->setEnabled(!georeference && m_ui->checkBoxMove->isChecked());
	bool customOrigin = !georeference && m_ui->checkBoxCustomOrigin->isChecked();
	m_ui->labelX->setEnabled(customOrigin);
	m_ui->labelY->setEnabled(customOrigin);
	m_ui->lineEditCustomCenterX->setEnabled(customOrigin);
	m_ui->lineEditCustomCenterY->setEnabled(customOrigin);

	QString tooltip;
	if (georeference)
		tooltip = tr("Disabled, the drawing is placed through its coordinate reference system.");
	m_ui->checkBoxMove->setToolTip(tooltip);
	m_ui->checkBoxCustomOrigin->setToolTip(tooltip);
}


void ImportDXFDialog::setGeoreferenceInfo(const QString & text, bool warning) {
	m_ui->labelGeoreferenceInfo->setText(text);
	m_ui->labelGeoreferenceInfo->setStyleSheet(warning ? "color: #c04000;" : QString());
}


void ImportDXFDialog::detectGeoreferencing() {
	m_coordinateSystem = Georeferencing::CoordinateSystem();
	m_designTransform = Georeferencing::DesignTransform();
	m_georeferenced = false;

	m_coordinateSystem = Georeferencing::detectCoordinateSystem(m_filePath, m_designTransform);

	QString projectInfo;
	if (m_haveProjectOrigin)
		projectInfo = tr(" The drawing is placed relative to the world coordinate origin of the project in %1.")
					  .arg(Georeferencing::utmName(m_worldUtmZone, m_worldNorth));

	if (!m_coordinateSystem.isValid()) {
		m_ui->checkBoxGeoreference->setChecked(false);
		m_ui->lineEditCRS->clear();
		setGeoreferenceInfo(tr("No coordinate reference system found in the DXF file. Enter it manually, "
							   "for example 'EPSG:25833', to place the drawing at its real world position."), false);
	}
	else {
		m_ui->checkBoxGeoreference->setChecked(true);
		m_ui->lineEditCRS->setText(m_coordinateSystem.m_name);
		if (m_designTransform.m_fromGeoData)
			setGeoreferenceInfo(tr("Coordinate reference system %1 read from the %2, including the placement "
								   "of the drawing.%3")
								.arg(m_coordinateSystem.m_name).arg(m_coordinateSystem.m_source).arg(projectInfo), false);
		else
			setGeoreferenceInfo(tr("Coordinate reference system %1 read from the %2.%3")
								.arg(m_coordinateSystem.m_name).arg(m_coordinateSystem.m_source).arg(projectInfo), false);
	}

	updateGeoreferenceControls();
}


IBKMK::Vector2D ImportDXFDialog::referencePoint(const Drawing & drawing) {
	double minX =  std::numeric_limits<double>::max();
	double minY =  std::numeric_limits<double>::max();
	double maxX = -std::numeric_limits<double>::max();
	double maxY = -std::numeric_limits<double>::max();

	auto addPoint = [&](const IBKMK::Vector2D & p) {
		minX = std::min(minX, p.m_x);
		minY = std::min(minY, p.m_y);
		maxX = std::max(maxX, p.m_x);
		maxY = std::max(maxY, p.m_y);
	};

	// objects that belong to a block are stored in block local coordinates and must be skipped
	for (const Drawing::Point & o : drawing.m_points)
		if (o.m_block == nullptr) addPoint(o.m_point);
	for (const Drawing::Line & o : drawing.m_lines)
		if (o.m_block == nullptr) { addPoint(o.m_point1); addPoint(o.m_point2); }
	for (const Drawing::PolyLine & o : drawing.m_polylines)
		if (o.m_block == nullptr) for (const IBKMK::Vector2D & p : o.m_polyline) addPoint(p);
	for (const Drawing::Circle & o : drawing.m_circles)
		if (o.m_block == nullptr) addPoint(o.m_center);
	for (const Drawing::Arc & o : drawing.m_arcs)
		if (o.m_block == nullptr) addPoint(o.m_center);
	for (const Drawing::Ellipse & o : drawing.m_ellipses)
		if (o.m_block == nullptr) addPoint(o.m_center);
	for (const Drawing::Solid & o : drawing.m_solids)
		if (o.m_block == nullptr) { addPoint(o.m_point1); addPoint(o.m_point2);
									addPoint(o.m_point3); addPoint(o.m_point4); }
	for (const Drawing::Text & o : drawing.m_texts)
		if (o.m_block == nullptr) addPoint(o.m_basePoint);
	for (const Drawing::Insert & o : drawing.m_inserts)
		addPoint(o.m_insertionPoint);

	if (minX > maxX)
		return IBKMK::Vector2D(0,0); // drawing without geometry

	return IBKMK::Vector2D(0.5*(minX + maxX), 0.5*(minY + maxY));
}


bool ImportDXFDialog::applyGeoreferencing(QString & log) {
	QString crsText = m_ui->lineEditCRS->text().trimmed();

	// what the user typed wins over what was detected
	Georeferencing::CoordinateSystem crs = m_coordinateSystem;
	if (crsText != m_coordinateSystem.m_name)
		crs = Georeferencing::fromUserInput(crsText);

	if (!crs.isValid()) {
		setGeoreferenceInfo(tr("Unknown coordinate reference system '%1', the drawing is imported without "
							   "georeferencing.").arg(crsText), true);
		log += QString("Georeferencing skipped: unknown coordinate reference system '%1'.\n").arg(crsText);
		return false;
	}

	// without an AcDbGeoData object the drawing coordinates are CRS coordinates already and only the
	// drawing unit has to be converted to meters
	Georeferencing::DesignTransform design = m_designTransform;
	if (!design.m_fromGeoData)
		design.m_scale = m_drawing.m_scalingFactor;

	IBKMK::Vector2D refPoint = referencePoint(m_drawing);

	int utmZone = m_worldUtmZone;
	bool north = m_worldNorth;
	if (!m_haveProjectOrigin) {
		utmZone = Georeferencing::utmZoneForPoint(crs, Georeferencing::drawingToCRS(design, refPoint), north);
		if (utmZone < 1) {
			setGeoreferenceInfo(tr("Cannot determine the UTM zone of the drawing, the drawing is imported "
								   "without georeferencing."), true);
			log += "Georeferencing skipped: cannot determine the UTM zone of the drawing.\n";
			return false;
		}
	}

	Georeferencing::Placement placement;
	QString errmsg;
	if (!Georeferencing::computePlacement(crs, design, utmZone, north, refPoint, placement, errmsg)) {
		setGeoreferenceInfo(errmsg, true);
		log += QString("Georeferencing skipped: %1\n").arg(errmsg);
		return false;
	}

	if (!m_haveProjectOrigin) {
		// the world coordinate origin keeps the local coordinates small, put it at the drawing center
		m_worldOrigin = IBKMK::Vector3D(std::floor(placement.m_referenceUtm.m_x),
										std::floor(placement.m_referenceUtm.m_y), 0);
		m_worldUtmZone = utmZone;
		m_worldNorth = north;
	}

	double rotationInDeg = placement.m_rotation/IBK::DEG2RAD;

	m_drawing.m_scalingFactor = placement.m_scale;
	m_drawing.m_rotationMatrix = RotationMatrix(QQuaternion::fromAxisAndAngle(0, 0, 1, (float)rotationInDeg));
	m_drawing.m_offset = IBKMK::Vector3D(placement.m_translation.m_x - m_worldOrigin.m_x,
										 placement.m_translation.m_y - m_worldOrigin.m_y, 0);

	setGeoreferenceInfo(tr("Drawing placed in %1 from %2. Rotation %L3 \u00b0, scaling factor %L4.")
						.arg(Georeferencing::utmName(m_worldUtmZone, m_worldNorth))
						.arg(crs.m_name).arg(rotationInDeg, 0, 'f', 4).arg(placement.m_scale), false);

	log += QString("Georeferenced from %1 to %2.\n").arg(crs.m_name)
		   .arg(Georeferencing::utmName(m_worldUtmZone, m_worldNorth));
	log += QString("World coordinate origin - X: %1 Y: %2\n").arg(m_worldOrigin.m_x, 0, 'f', 3)
		   .arg(m_worldOrigin.m_y, 0, 'f', 3);
	log += QString("Rotation: %1 deg, scaling factor: %2\n").arg(rotationInDeg, 0, 'f', 4).arg(placement.m_scale);
	log += QString("---------------------------------------------------------\n");

	return true;
}


void ImportDXFDialog::updateImportButtonEnabledState() {
	bool valid = true;
	if (m_ui->checkBoxCustomOrigin->isChecked())
		valid = m_ui->lineEditCustomCenterX->isValid() && m_ui->lineEditCustomCenterY->isValid();
	m_ui->pushButtonImport->setEnabled(valid);
}


bool ImportDXFDialog::readDxfFile(Drawing &drawing, const QString &fname) {
	DRW_InterfaceImpl drwIntImpl(&drawing, &m_dxfScalingFactor, &m_dxfScalingUnit, m_nextId);
	//	dxfRW dxf(fname.toStdString().c_str());
	dxfRW dxf(fname.toStdString());

	bool success = dxf.read(&drwIntImpl, false);
	return success;
}

template <typename t>
void movePoints(const IBKMK::Vector2D &center, std::vector<t> &objects) {
	for (Drawing::AbstractDrawingObject &obj: objects) {
		for (const IBKMK::Vector2D &v2D : obj.points2D()) {
			const_cast<IBKMK::Vector2D &>(v2D) -= center;
		}
	}
}


void ImportDXFDialog::fixFonts() {
	double HEIGHT = 15;

	for (Drawing::Text &text : m_drawing.m_texts) {
		if (text.m_height > HEIGHT)
			text.m_height *= 0.1;
	}
	for (Drawing::LinearDimension &linDem : m_drawing.m_linearDimensions) {
		if (linDem.m_style->m_textHeight > HEIGHT)
			linDem.m_style->m_textHeight *= 0.1;
	}
}

const Drawing& ImportDXFDialog::drawing() const {
	return m_drawing;
}


template <typename t>
void drawingBoundingBox(const Drawing &d,
						const std::vector<t> &drawingObjects,
						IBKMK::Vector3D &upperValues,
						IBKMK::Vector3D &lowerValues,
						const IBKMK::Vector3D &offset = IBKMK::Vector3D(0,0,0),
						const IBKMK::Vector3D &xAxis = IBKMK::Vector3D(1,0,0),
						const IBKMK::Vector3D &yAxis = IBKMK::Vector3D(0,1,0),
						const IBKMK::Vector3D &zAxis = IBKMK::Vector3D(0,0,1)) {

	// store selected surfaces
	if (drawingObjects.empty())
		return;

	// process all drawings
	for (const t &drawObj : drawingObjects) {
		const DrawingLayer *dl = dynamic_cast<const DrawingLayer *>(drawObj.m_layerRef);

		Q_ASSERT(dl != nullptr);

		if (!dl->m_visible)
			continue;

		if (dl->m_displayName == "0")
			continue; // Skipping historic layer 0 for better bounding box results

		const std::vector<IBKMK::Vector3D> &points = d.points3D(drawObj.points2D(), drawObj);

		for (const IBKMK::Vector3D &v : points) {

			IBKMK::Vector3D vLocal, point;

			IBKMK::lineToPointDistance(offset, xAxis, v, vLocal.m_x, point);
			IBKMK::lineToPointDistance(offset, yAxis, v, vLocal.m_y, point);
			IBKMK::lineToPointDistance(offset, zAxis, v, vLocal.m_z, point);

			upperValues.m_x = std::max(upperValues.m_x, (double)vLocal.m_x);
			upperValues.m_y = std::max(upperValues.m_y, (double)vLocal.m_y);
			upperValues.m_z = std::max(upperValues.m_z, (double)vLocal.m_z);

			lowerValues.m_x = std::min(lowerValues.m_x, (double)vLocal.m_x);
			lowerValues.m_y = std::min(lowerValues.m_y, (double)vLocal.m_y);
			lowerValues.m_z = std::min(lowerValues.m_z, (double)vLocal.m_z);
		}
	}
}


IBKMK::Vector3D ImportDXFDialog::boundingBox(const Drawing *drawing, IBKMK::Vector3D & center, bool transformPoints, const double scalingFactor = 1.0) {

	IBKMK::Vector3D lowerValues(std::numeric_limits<double>::max(),
								std::numeric_limits<double>::max(),
								std::numeric_limits<double>::max());
	IBKMK::Vector3D upperValues(std::numeric_limits<double>::lowest(),
								std::numeric_limits<double>::lowest(),
								std::numeric_limits<double>::lowest());

	drawingBoundingBox<Drawing::Arc>(*drawing, drawing->m_arcs, upperValues, lowerValues);
	drawingBoundingBox<Drawing::Circle>(*drawing, drawing->m_circles, upperValues, lowerValues);
	drawingBoundingBox<Drawing::Ellipse>(*drawing, drawing->m_ellipses, upperValues, lowerValues);
	drawingBoundingBox<Drawing::Line>(*drawing, drawing->m_lines, upperValues, lowerValues);
	drawingBoundingBox<Drawing::PolyLine>(*drawing, drawing->m_polylines, upperValues, lowerValues);
	drawingBoundingBox<Drawing::Point>(*drawing, drawing->m_points, upperValues, lowerValues);
	drawingBoundingBox<Drawing::Solid>(*drawing, drawing->m_solids, upperValues, lowerValues);
	drawingBoundingBox<Drawing::Text>(*drawing, drawing->m_texts, upperValues, lowerValues);
	drawingBoundingBox<Drawing::LinearDimension>(*drawing, drawing->m_linearDimensions, upperValues, lowerValues);

	// center point of bounding box
	center = 0.5 * scalingFactor * (lowerValues+upperValues);
	// difference between upper and lower values gives bounding box (dimensions of selected geometry)
	return scalingFactor * (upperValues-lowerValues);
}


void ImportDXFDialog::on_comboBoxUnit_activated(int index) {
	m_ui->comboBoxUnit->setCurrentIndex(index);
}


DRW_InterfaceImpl::DRW_InterfaceImpl(Drawing *drawing, double *dxfScalingFactor,
									 std::string *dxfScalingUnit, unsigned int &nextId) :
	m_drawing(drawing),
	m_nextId(&nextId),
	m_dxfScalingFactor(dxfScalingFactor),
	m_dxfScalingUnit(dxfScalingUnit)
{}

// Function to get the unit name and scaling factor relative to meters from INSUNITS value
std::pair<std::string, double> getUnitInfo(int insunits) {
	// INSUNITS value to units mapping
	std::map<int, std::pair<std::string, double>> unitsMapping = {
		{0, {"Unitless", 1.0}},
		{1, {"Inches", 0.0254}},
		{2, {"Feet", 0.3048}},
		{3, {"Miles", 1609.34}},
		{4, {"Millimeters", 0.001}},
		{5, {"Centimeters", 0.01}},
		{6, {"Meters", 1.0}},
		{7, {"Kilometers", 1000.0}},
		{8, {"Microinches", 0.0000000254}},
		{9, {"Mils", 0.0000254}},
		{10, {"Yards", 0.9144}},
		{11, {"Angstroms", 0.0000000001}},
		{12, {"Nanometers", 0.000000001}},
		{13, {"Microns", 0.000001}},
		{14, {"Decimeters", 0.1}},
		{15, {"Decameters", 10.0}},
		{16, {"Hectometers", 100.0}},
		{17, {"Gigameters", 1000000000.0}},
		{18, {"Astronomical units", 149597870700.0}},
		{19, {"Light years", 9.4607e15}},
		{20, {"Parsecs", 3.0857e16}}
	};

	// Find the unit in the map
	auto it = unitsMapping.find(insunits);
	if (it != unitsMapping.end()) {
		return it->second;
	} else {
		return {"Unknown", 1.0};
	}
}

void DRW_InterfaceImpl::addHeader(const DRW_Header* data){
	if (data->vars.find("$INSUNITS") == data->vars.end())
		return;

	DRW_Variant *var = data->vars.at("$INSUNITS");
	if (var == nullptr)
		return;

	int unitCode = var->content.i;

	std::pair<std::string, double> scalingFactor = getUnitInfo(unitCode);
	*m_dxfScalingFactor = scalingFactor.second;
	*m_dxfScalingUnit = scalingFactor.first;
}

void DRW_InterfaceImpl::addLType(const DRW_LType& /*data*/){}
void DRW_InterfaceImpl::addLayer(const DRW_Layer& data){

	// only read ONE layer without name
	if (data.name.empty()) {
		if (m_emptyLayerExists)
			return;
		m_emptyLayerExists = true;
	}

	// If nullptr return
	if(m_activeBlock != nullptr)
		return;

	// initialise struct Layer and populate the attributes
	DrawingLayer newLayer;

	// Set ID
	newLayer.m_id = (*m_nextId)++;

	// name of layer
	newLayer.m_displayName = QString::fromStdString(data.name);

	// read linewidth from dxf file, convert to double using lineWidth2dxfInt from libdxfrw
	newLayer.m_lineWeight = DRW_LW_Conv::lineWidth2dxfInt(data.lWeight);

	// is visible?
	newLayer.m_visible = data.plotF;

	/* value 256 means use defaultColor, value 7 is black */
	if (data.color != 256 && data.color != 7)
		newLayer.m_color = QColor(DRW::dxfColors[data.color][0], DRW::dxfColors[data.color][1], DRW::dxfColors[data.color][2]);
	//	else
	//		newLayer.m_color = SVStyle::instance().m_defaultDrawingColor;

	// Push new layer into vector<Layer*> m_layer
	m_drawing->m_drawingLayers.push_back(newLayer);
}


void DRW_InterfaceImpl::addDimStyle(const DRW_Dimstyle& data) {
	Drawing::DimStyle dimStyle;
	dimStyle.m_name = QString::fromStdString(data.name);
	dimStyle.m_upperLineDistance = data.dimexe;
	dimStyle.m_extensionLineLowerDistance = data.dimexo;

	dimStyle.m_fixedExtensionLength = data.dimfxlon == 1;
	dimStyle.m_extensionLineLength = data.dimfxl;
	dimStyle.m_textHeight = data.dimtxt;
	dimStyle.m_globalScalingFactor = data.dimscale;
	dimStyle.m_textScalingFactor = data.dimtfac;
	dimStyle.m_textLinearFactor = data.dimlfac;
	dimStyle.m_textDecimalPlaces = data.dimdec;

	dimStyle.m_id = (*m_nextId)++;

	// Add object
	m_drawing->m_dimensionStyles.push_back(dimStyle);
}


void DRW_InterfaceImpl::addVport(const DRW_Vport& data){
	m_drawing->m_offset.m_x = (double)-1.0 * data.center.x;
	m_drawing->m_offset.m_y = (double)-1.0 * data.center.y;
	m_drawing->m_offset.m_z = (double)-1.0 * data.center.z;
}


void DRW_InterfaceImpl::addTextStyle(const DRW_Textstyle& /*data*/){}
void DRW_InterfaceImpl::addAppId(const DRW_AppId& /*data*/){}
void DRW_InterfaceImpl::addBlock(const DRW_Block& data){

	if (data.name.empty())
		return;

	// New Block
	Drawing::Block newBlock;

	// Set name
	newBlock.m_name = QString::fromStdString(data.name);

	// qDebug() << "Block name: " << newBlock.m_name;

	// Set color
	newBlock.m_color = QColor();

	// Set line weight
	newBlock.m_lineWeight = 0;

	// ID
	newBlock.m_id = (*m_nextId)++;

	// Set base
	newBlock.m_basePoint = IBKMK::Vector2D(data.basePoint.x, data.basePoint.y);

	m_drawing->m_blocks.push_back(newBlock);

	// Set actove block
	m_activeBlock = &m_drawing->m_blocks.back();
}


void DRW_InterfaceImpl::setBlock(const int /*handle*/){}
void DRW_InterfaceImpl::endBlock(){
	// if (m_activeBlock != nullptr)
	// 	m_activeBlock->m_basePoint = IBKMK::Vector2D(0,0);
	// Active block not existing
	m_activeBlock = nullptr;
}


void DRW_InterfaceImpl::addPoint(const DRW_Point& data){

	Drawing::Point newPoint;
	newPoint.m_zPosition = m_drawing->m_zCounter;
	m_drawing->m_zCounter++;

	//create new point, insert into vector m_points from drawing
	newPoint.m_point = IBKMK::Vector2D(data.basePoint.x, data.basePoint.y);
	newPoint.m_lineWeight = DRW_LW_Conv::lineWidth2dxfInt(data.lWeight);
	newPoint.m_layerName = QString::fromStdString(data.layer);

	newPoint.m_id = (*m_nextId)++;
	if (m_activeBlock != nullptr) {
		newPoint.m_blockName = m_activeBlock->m_name;
		// newPoint.m_point -= m_activeBlock->m_basePoint;
	}
	/* value 256 means use defaultColor, value 7 is black */
	if(!(data.color == 256 || data.color == 7))
		newPoint.m_color = QColor(DRW::dxfColors[data.color][0], DRW::dxfColors[data.color][1], DRW::dxfColors[data.color][2]);
	else
		newPoint.m_color = QColor();

	m_drawing->m_points.push_back(newPoint);

}


void DRW_InterfaceImpl::addLine(const DRW_Line& data){

	Drawing::Line newLine;
	newLine.m_zPosition = m_drawing->m_zCounter;
	m_drawing->m_zCounter++;

	//create new line, insert into vector m_lines from drawing
	newLine.m_point1 = IBKMK::Vector2D(data.basePoint.x, data.basePoint.y);
	newLine.m_point2 = IBKMK::Vector2D(data.secPoint.x, data.secPoint.y);
	newLine.m_lineWeight = DRW_LW_Conv::lineWidth2dxfInt(data.lWeight);
	newLine.m_layerName = QString::fromStdString(data.layer);

	newLine.m_id = (*m_nextId)++;

	if (m_activeBlock != nullptr) {
		newLine.m_blockName = m_activeBlock->m_name;
		// newLine.m_point1 -= m_activeBlock->m_basePoint;
		// newLine.m_point2 -= m_activeBlock->m_basePoint;
	}
	/* value 256 means use defaultColor, value 7 is black */
	/* value 256 means use defaultColor, value 7 is black */
	if(!(data.color == 256 || data.color == 7))
		newLine.m_color = QColor(DRW::dxfColors[data.color][0], DRW::dxfColors[data.color][1], DRW::dxfColors[data.color][2]);
	else
		newLine.m_color = QColor();

	m_drawing->m_lines.push_back(newLine);
}

void DRW_InterfaceImpl::addRay(const DRW_Ray& /*data*/){}
void DRW_InterfaceImpl::addXline(const DRW_Xline& /*data*/){}
void DRW_InterfaceImpl::addArc(const DRW_Arc& data){

	Drawing::Arc newArc;
	newArc.m_zPosition = m_drawing->m_zCounter;
	m_drawing->m_zCounter++;

	// create new arc, insert into vector m_arcs from drawing
	newArc.m_radius = data.radious;
	newArc.m_startAngle = data.staangle;
	newArc.m_endAngle = data.endangle;
	newArc.m_center = IBKMK::Vector2D(data.basePoint.x, data.basePoint.y);
	newArc.m_lineWeight = DRW_LW_Conv::lineWidth2dxfInt(data.lWeight);
	newArc.m_layerName = QString::fromStdString(data.layer);

	newArc.m_id = (*m_nextId)++;
	if (m_activeBlock != nullptr) {
		newArc.m_blockName = m_activeBlock->m_name;
		// newArc.m_center -= m_activeBlock->m_basePoint;
	}

	/* value 256 means use defaultColor, value 7 is black */
	if(!(data.color == 256 || data.color == 7))
		newArc.m_color = QColor(DRW::dxfColors[data.color][0], DRW::dxfColors[data.color][1], DRW::dxfColors[data.color][2]);
	else
		newArc.m_color = QColor();

	m_drawing->m_arcs.push_back(newArc);
}


void DRW_InterfaceImpl::addCircle(const DRW_Circle& data){

	Drawing::Circle newCircle;
	newCircle.m_zPosition = m_drawing->m_zCounter;
	m_drawing->m_zCounter++;

	newCircle.m_center = IBKMK::Vector2D(data.basePoint.x, data.basePoint.y);

	newCircle.m_radius = data.radious;
	newCircle.m_lineWeight = DRW_LW_Conv::lineWidth2dxfInt(data.lWeight);
	newCircle.m_layerName = QString::fromStdString(data.layer);

	newCircle.m_id = (*m_nextId)++;
	if (m_activeBlock != nullptr) {
		newCircle.m_blockName = m_activeBlock->m_name;
		// newCircle.m_center -= m_activeBlock->m_basePoint;
	}

	/* value 256 means use defaultColor, value 7 is black */
	if(!(data.color == 256 || data.color == 7))
		newCircle.m_color = QColor(DRW::dxfColors[data.color][0], DRW::dxfColors[data.color][1], DRW::dxfColors[data.color][2]);
	else
		newCircle.m_color = QColor();

	m_drawing->m_circles.push_back(newCircle);
}


void DRW_InterfaceImpl::addEllipse(const DRW_Ellipse& data){

	Drawing::Ellipse newEllipse;
	newEllipse.m_zPosition = m_drawing->m_zCounter;
	m_drawing->m_zCounter++;

	newEllipse.m_center = IBKMK::Vector2D(data.basePoint.x, data.basePoint.y);
	newEllipse.m_majorAxis = IBKMK::Vector2D(data.secPoint.x, data.secPoint.y);
	newEllipse.m_ratio = data.ratio;
	newEllipse.m_startAngle = data.staparam;
	newEllipse.m_endAngle = data.endparam;
	newEllipse.m_lineWeight = DRW_LW_Conv::lineWidth2dxfInt(data.lWeight);
	newEllipse.m_layerName = QString::fromStdString(data.layer);

	newEllipse.m_id = (*m_nextId)++;
	if (m_activeBlock != nullptr) {
		newEllipse.m_blockName = m_activeBlock->m_name;
		// newEllipse.m_center -= m_activeBlock->m_basePoint;
	}

	/* value 256 means use defaultColor, value 7 is black */
	if(!(data.color == 256 || data.color == 7))
		newEllipse.m_color = QColor(DRW::dxfColors[data.color][0], DRW::dxfColors[data.color][1], DRW::dxfColors[data.color][2]);
	else
		newEllipse.m_color = QColor();

	m_drawing->m_ellipses.push_back(newEllipse);
}


void DRW_InterfaceImpl::addLWPolyline(const DRW_LWPolyline& data){

	Drawing::PolyLine newPolyline;
	newPolyline.m_zPosition = m_drawing->m_zCounter;
	m_drawing->m_zCounter++;

	newPolyline.m_polyline = std::vector<IBKMK::Vector2D>();
	newPolyline.m_lineWeight = DRW_LW_Conv::lineWidth2dxfInt(data.lWeight);

	newPolyline.m_id = (*m_nextId)++;

	// iterate over data.vertlist, insert all vertices of Polyline into vector
	for(size_t i = 0; i < data.vertlist.size(); i++){
		IBKMK::Vector2D point(data.vertlist[i]->x, data.vertlist[i]->y);
		newPolyline.m_polyline.push_back(point);

		// qDebug() << QString("PL Point | %1 %2").arg(point.m_x).arg(point.m_y);
	}

	if (m_activeBlock != nullptr) {
		newPolyline.m_blockName = m_activeBlock->m_name;
		// for(IBKMK::Vector2D &pl : newPolyline.m_polyline){
		// 	pl -= m_activeBlock->m_basePoint;
		// }
	}


	newPolyline.m_layerName = QString::fromStdString(data.layer);

	/* value 256 means use defaultColor, value 7 is black */
	if (!(data.color == 256 || data.color == 7))
		newPolyline.m_color = QColor(DRW::dxfColors[data.color][0], DRW::dxfColors[data.color][1], DRW::dxfColors[data.color][2]);
	else
		newPolyline.m_color = QColor();

	newPolyline.m_endConnected = data.flags == 129 || data.flags == 1 ;

	// insert vector into m_lines[data.layer] vector
	m_drawing->m_polylines.push_back(newPolyline);
}


void DRW_InterfaceImpl::addPolyline(const DRW_Polyline& data){

	Drawing::PolyLine newPolyline;
	newPolyline.m_zPosition = m_drawing->m_zCounter;
	m_drawing->m_zCounter++;
	newPolyline.m_polyline = std::vector<IBKMK::Vector2D>();

	// if data.vertexcount is not set, use size of vertlist
	int vertexcount = data.vertexcount;
	if(vertexcount == 0) vertexcount = data.vertlist.size();

	// qDebug() << " ";

	// iterateover data.vertlist, insert all vertices of Polyline into vector
	for (int i = 0; i < vertexcount; i++){
		IBKMK::Vector2D point(data.vertlist[i]->basePoint.x, data.vertlist[i]->basePoint.y);
		// if(m_activeBlock != nullptr) {
		// 	point -= m_activeBlock->m_basePoint;
		// }

		// qDebug() << QString("PL Point | %1 %2").arg(point.m_x).arg(point.m_y);

		newPolyline.m_polyline.push_back(point);
	}

	newPolyline.m_id = (*m_nextId)++;
	if (m_activeBlock != nullptr)
		newPolyline.m_blockName = m_activeBlock->m_name;

	newPolyline.m_layerName = QString::fromStdString(data.layer);
	newPolyline.m_lineWeight = DRW_LW_Conv::lineWidth2dxfInt(data.lWeight);

	/* value 256 means use defaultColor, value 7 is black */
	if(!(data.color == 256 || data.color == 7))
		newPolyline.m_color = QColor(DRW::dxfColors[data.color][0], DRW::dxfColors[data.color][1], DRW::dxfColors[data.color][2]);
	else
		newPolyline.m_color = QColor();

	newPolyline.m_endConnected = data.flags == 129 || data.flags == 1 ;

	// insert vector into m_lines[data.layer] vector
	m_drawing->m_polylines.push_back(newPolyline);
}
void DRW_InterfaceImpl::addSpline(const DRW_Spline* /*data*/){}
void DRW_InterfaceImpl::addKnot(const DRW_Entity & /*data*/){}

void DRW_InterfaceImpl::addInsert(const DRW_Insert& data){
	if (data.name.empty())
		return;

	Drawing::Insert newInsert;
	newInsert.m_currentBlockName = QString::fromStdString(data.name);

	newInsert.m_angle = data.angle;

	newInsert.m_xScale = data.xscale;
	newInsert.m_yScale = data.yscale;
	newInsert.m_zScale = data.zscale;

	newInsert.m_insertionPoint = IBKMK::Vector2D(data.basePoint.x, data.basePoint.y);
	if (m_activeBlock != nullptr) {
		newInsert.m_parentBlockName = m_activeBlock->m_name;
		// newInsert.m_insertionPoint -= m_activeBlock->m_basePoint;
	}

	m_drawing->m_inserts.push_back(newInsert);
}

void DRW_InterfaceImpl::addTrace(const DRW_Trace& /*data*/){}
void DRW_InterfaceImpl::add3dFace(const DRW_3Dface& data){
	// auto printPoint = [](const DRW_Coord &coord, const QString &text) {
	// 	qDebug() << QString("%4 | Point X %1 Y %2 Z %3").arg(coord.x, coord.y, coord.z).arg(text);
	// };

	// printPoint(data.basePoint, "p0");
	// printPoint(data.secPoint, "p1");
	// printPoint(data.thirdPoint, "p2");
	// printPoint(data.fourPoint, "p3");
}


void DRW_InterfaceImpl::addSolid(const DRW_Solid& data){

	Drawing::Solid newSolid;
	newSolid.m_zPosition = m_drawing->m_zCounter;
	m_drawing->m_zCounter++;

	newSolid.m_point1 = IBKMK::Vector2D(data.basePoint.x, data.basePoint.y);
	newSolid.m_point2 = IBKMK::Vector2D(data.secPoint.x, data.secPoint.y);
	newSolid.m_point3 = IBKMK::Vector2D(data.fourPoint.x, data.fourPoint.y);
	newSolid.m_point4 = IBKMK::Vector2D(data.thirdPoint.x, data.thirdPoint.y);

	newSolid.m_lineWeight = DRW_LW_Conv::lineWidth2dxfInt(data.lWeight);
	newSolid.m_layerName = QString::fromStdString(data.layer);

	newSolid.m_id = (*m_nextId)++;
	if (m_activeBlock != nullptr) {
		newSolid.m_blockName = m_activeBlock->m_name;
		// newSolid.m_point1 -= m_activeBlock->m_basePoint;
		// newSolid.m_point2 -= m_activeBlock->m_basePoint;
		// newSolid.m_point3 -= m_activeBlock->m_basePoint;
		// newSolid.m_point4 -= m_activeBlock->m_basePoint;
	}

	/* value 256 means use defaultColor, value 7 is black */
	if(!(data.color == 256 || data.color == 7))
		newSolid.m_color = QColor(DRW::dxfColors[data.color][0], DRW::dxfColors[data.color][1], DRW::dxfColors[data.color][2]);
	else
		newSolid.m_color = QColor();

	m_drawing->m_solids.push_back(newSolid);

}


std::string replaceFormatting(const std::string &str) {
	std::string replaced = str;

	try {
		// 0) Protect literal backslashes "\\" -> placeholder (ASCII 26)
		replaced = std::regex_replace(
					replaced,
					std::regex(R"(\\\\)"),
					std::string(1, '\x1A')
					);

		// 1) Replace DXF paragraph breaks and real newlines/tabs with a space
		replaced = std::regex_replace(
					replaced,
					std::regex(R"(\\P|\n|\t)"),
					" "
					);

		// 2) Drop \pt...; positioning codes (present in your DXF)
		replaced = std::regex_replace(
					replaced,
					std::regex(R"(\\pt[^;]*;)"),
					""
					);

		// 3) TRUE stacked fractions: \Snum/den; or \Snum#den; or \Snum^den;
		//    -> "num/den"
		replaced = std::regex_replace(
					replaced,
					std::regex(R"(\\S([^/#\^;]+)[/#\^]([^;]+);)"),
					"$1/$2"
					);

		// 4) Superscripts: map \S1^  ;, \S2^  ;, \S3^  ; to Unicode ¹²³
		//    (used e.g. for "m²" in your DXF)
		replaced = std::regex_replace(
					replaced,
					std::regex(R"(\\S1\^ *;)"),
					"\xC2\xB9"   // ¹
					);
		replaced = std::regex_replace(
					replaced,
					std::regex(R"(\\S2\^ *;)"),
					"\xC2\xB2"   // ²
					);
		replaced = std::regex_replace(
					replaced,
					std::regex(R"(\\S3\^ *;)"),
					"\xC2\xB3"   // ³
					);

		// 5) Remove any remaining \S...; (baseline shifts etc.)
		replaced = std::regex_replace(
					replaced,
					std::regex(R"(\\S[^;]*;)"),
					""
					);

		// 6) Remove stray slash after superscript (e.g. "m²/")
		replaced = std::regex_replace(
					replaced,
					std::regex(R"((\xC2\xB9|\xC2\xB2|\xC2\xB3)/)"),
					"$1"
					);

		// 7) Strip inline formatting codes like \A, \C, \F, \H, \L, \O, \Q, \T, \W
		//    with or without argument blocks: \C1;  \H0.7x;  \L  etc.
		replaced = std::regex_replace(
					replaced,
					std::regex(R"(\\[ACcFfHLlOopQTW](?:[^\\;]*;)?)"),
					""
					);

		// 8) Remove MTEXT grouping braces { ... }, including "{/"
		replaced = std::regex_replace(
					replaced,
					std::regex(R"(\{/?|\})"),
					""
					);

		// 9) Restore literal backslashes from placeholder
		std::replace(replaced.begin(), replaced.end(), '\x1A', '\\');

		// 10) Collapse repeated spaces (and tabs) to a single space
		replaced = std::regex_replace(
					replaced,
					std::regex(R"([ \t]+)"),
					" "
					);

		// 11) Trim leading/trailing whitespace
		auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };

		auto it_begin = std::find_if_not(replaced.begin(), replaced.end(), is_space);
		auto it_end   = std::find_if_not(replaced.rbegin(), replaced.rend(), is_space).base();

		if (it_begin < it_end)
			replaced = std::string(it_begin, it_end);
		else
			replaced.clear();
	}
	catch (const std::exception &ex) {
		// Replace this with your own logging if you don't use IBK:
		IBK::IBK_Message(
					IBK::FormatString("Could not replace all regex expressions.\n%1").arg(ex.what()),
					IBK::MSG_WARNING
					);
	}

	return replaced;
}




void DRW_InterfaceImpl::addMText(const DRW_MText& data){
	Drawing::Text newText;
	newText.m_text = QString::fromStdString(replaceFormatting(data.text));
	newText.m_basePoint = IBKMK::Vector2D(data.basePoint.x, data.basePoint.y);
	newText.m_zPosition = m_drawing->m_zCounter;
	m_drawing->m_zCounter++;
	newText.m_id = (*m_nextId)++;
	if (m_activeBlock != nullptr) {
		newText.m_blockName = m_activeBlock->m_name;
		// newText.m_basePoint -= m_activeBlock->m_basePoint;
	}

	newText.m_lineWeight = DRW_LW_Conv::lineWidth2dxfInt(data.lWeight);
	newText.m_layerName = QString::fromStdString(data.layer);
	newText.m_height = data.height;
	newText.m_alignment = data.alignH == DRW_Text::HCenter ? Qt::AlignHCenter : Qt::AlignLeft;
	newText.m_rotationAngle = data.angle;

	/* value 256 means use defaultColor, value 7 is black */
	if(!(data.color == 256 || data.color == 7))
		newText.m_color = QColor(DRW::dxfColors[data.color][0], DRW::dxfColors[data.color][1], DRW::dxfColors[data.color][2]);
	else
		newText.m_color = QColor();

	m_drawing->m_texts.push_back(newText);
}

void DRW_InterfaceImpl::addText(const DRW_Text& data){

	Drawing::Text newText;
	newText.m_text = QString::fromStdString(data.text);
	newText.m_basePoint = IBKMK::Vector2D(data.basePoint.x, data.basePoint.y);
	newText.m_zPosition = m_drawing->m_zCounter;
	m_drawing->m_zCounter++;
	newText.m_id = (*m_nextId)++;
	if (m_activeBlock != nullptr) {
		newText.m_blockName = m_activeBlock->m_name;
		// newText.m_basePoint -= m_activeBlock->m_basePoint;
	}

	newText.m_lineWeight = DRW_LW_Conv::lineWidth2dxfInt(data.lWeight);
	newText.m_layerName = QString::fromStdString(data.layer);
	newText.m_height = data.height;
	newText.m_alignment = data.alignH == DRW_Text::HCenter ? Qt::AlignHCenter : Qt::AlignLeft;
	newText.m_rotationAngle = data.angle;

	/* value 256 means use defaultColor, value 7 is black */
	if(!(data.color == 256 || data.color == 7))
		newText.m_color = QColor(DRW::dxfColors[data.color][0],
								 DRW::dxfColors[data.color][1],
								 DRW::dxfColors[data.color][2]);
	else
		newText.m_color = QColor();

	m_drawing->m_texts.push_back(newText);
}
void DRW_InterfaceImpl::addDimAlign(const DRW_DimAligned */*data*/){}
void DRW_InterfaceImpl::addDimLinear(const DRW_DimLinear *data){

	// Line points
	const DRW_Coord &def1Point = data->getDef1Point();
	const DRW_Coord &def2Point = data->getDef2Point();
	const DRW_Coord &defPoint = data->getDefPoint();
	const DRW_Coord &textPoint = data->getTextPoint();

	// Definitions
	IBKMK::Vector2D def (defPoint.x, defPoint.y);
	IBKMK::Vector2D def1 (def1Point.x, def1Point.y);
	IBKMK::Vector2D def2 (def2Point.x, def2Point.y);
	IBKMK::Vector2D text (textPoint.x, textPoint.y);

	Drawing::LinearDimension newLinearDimension;

	newLinearDimension.m_zPosition = m_drawing->m_zCounter;
	m_drawing->m_zCounter++;
	newLinearDimension.m_id = (*m_nextId)++;
	if (m_activeBlock != nullptr) {
		newLinearDimension.m_blockName = m_activeBlock->m_name;
		// def -= m_activeBlock->m_basePoint;
		// def1 -= m_activeBlock->m_basePoint;
		// def2 -= m_activeBlock->m_basePoint;
		// text -= m_activeBlock->m_basePoint;
	}

	newLinearDimension.m_lineWeight = DRW_LW_Conv::lineWidth2dxfInt(data->lWeight);
	newLinearDimension.m_layerName = QString::fromStdString(data->layer);
	newLinearDimension.m_point1 = def1;
	newLinearDimension.m_point2 = def2;
	newLinearDimension.m_dimensionPoint = def;
	newLinearDimension.m_textPoint = text;
	newLinearDimension.m_angle = data->getAngle();
	newLinearDimension.m_measurement = data->getText().c_str();
	newLinearDimension.m_styleName = QString::fromStdString(data->getStyle());

	// value 256 means use defaultColor, value 7 is black
	if(!(data->color == 256 || data->color == 7))
		newLinearDimension.m_color = QColor(DRW::dxfColors[data->color][0],
											DRW::dxfColors[data->color][1],
											DRW::dxfColors[data->color][2]);
	else
		newLinearDimension.m_color = QColor();


	/// Linear dimension needs to be fully constructed.
	/// Dimension point can belong to point 1 or 2 and has also an angle
	/// For more information look here:
	/// https://ezdxf.readthedocs.io/en/stable/tutorials/linear_dimension.html

	IBKMK::Vector2D xAxis(1, 0);
	IBKMK::Vector2D lineVec (std::cos(newLinearDimension.m_angle * IBK::DEG2RAD),
							 std::sin(newLinearDimension.m_angle * IBK::DEG2RAD));

	IBKMK::Vector2D lineVec2 (lineVec.m_y, -lineVec.m_x);

	const unsigned int SCALING_FACTOR = 1E6;

	IBKMK::Vector2D measurePoint1 = newLinearDimension.m_point1 + SCALING_FACTOR * lineVec2;
	IBKMK::Vector2D measurePoint2 = newLinearDimension.m_point2 + SCALING_FACTOR * lineVec2;

	IBK::Line lineMeasure (newLinearDimension.m_dimensionPoint -   SCALING_FACTOR * lineVec,
						   newLinearDimension.m_dimensionPoint + 2*SCALING_FACTOR * lineVec);

	IBK::Line lineLeft  (newLinearDimension.m_point1 - SCALING_FACTOR * lineVec2, measurePoint1);
	IBK::Line lineRight (newLinearDimension.m_point2 - SCALING_FACTOR * lineVec2, measurePoint2);

	IBKMK::Vector2D intersection1Left, intersection2Left;
	IBKMK::Vector2D intersection1Right, intersection2Right;
	bool intersect1 = lineMeasure.intersects(lineLeft, intersection1Left, intersection2Left) == 1;
	bool intersect2 = lineMeasure.intersects(lineRight, intersection1Right, intersection2Right) == 1;

	if (!intersect1 && !intersect2) {
		IBK::IBK_Message(IBK::FormatString("Linear dimension seems broken. Skipping."), IBK::MSG_WARNING);
		return;
	}

	IBKMK::Vector2D leftPoint, rightPoint;
	IBKMK::Vector2D point1, point2;
	bool left = false, right = false;
	if (intersect1 && (newLinearDimension.m_dimensionPoint - intersection1Left).magnitudeSquared() > 1E-3 ) {
		newLinearDimension.m_leftPoint = intersection1Left;
		newLinearDimension.m_rightPoint = newLinearDimension.m_dimensionPoint;
		left = true;
	}
	if (intersect2 && (newLinearDimension.m_dimensionPoint - intersection1Right).magnitudeSquared() > 1E-3 ) {
		newLinearDimension.m_leftPoint = newLinearDimension.m_dimensionPoint;
		newLinearDimension.m_rightPoint = intersection1Right;
		right = true;
	}

	if (!left && !right) {
		IBK::IBK_Message(IBK::FormatString("Linear dimension seems broken. Skipping."), IBK::MSG_WARNING);
		return;
	}

	m_drawing->m_linearDimensions.push_back(newLinearDimension);

}

void DRW_InterfaceImpl::addDimRadial(const DRW_DimRadial */*data*/){}
void DRW_InterfaceImpl::addDimDiametric(const DRW_DimDiametric */*data*/){}
void DRW_InterfaceImpl::addDimAngular(const DRW_DimAngular */*data*/){}
void DRW_InterfaceImpl::addDimAngular3P(const DRW_DimAngular3p */*data*/){}
void DRW_InterfaceImpl::addDimOrdinate(const DRW_DimOrdinate */*data*/){}
void DRW_InterfaceImpl::addLeader(const DRW_Leader */*data*/){}
void DRW_InterfaceImpl::addHatch(const DRW_Hatch */*data*/){}

void DRW_InterfaceImpl::addViewport(const DRW_Viewport& /*data*/) {}

void DRW_InterfaceImpl::addImage(const DRW_Image */*data*/){}
void DRW_InterfaceImpl::linkImage(const DRW_ImageDef */*data*/){}
void DRW_InterfaceImpl::addComment(const char* /*comment*/){}

// no need to implement
void DRW_InterfaceImpl::writeHeader(DRW_Header& /*data*/){}
void DRW_InterfaceImpl::writeBlocks(){}
void DRW_InterfaceImpl::writeBlockRecords(){}
void DRW_InterfaceImpl::writeEntities(){}
void DRW_InterfaceImpl::writeLTypes(){}
void DRW_InterfaceImpl::writeLayers(){}
void DRW_InterfaceImpl::writeTextstyles(){}
void DRW_InterfaceImpl::writeVports(){}
void DRW_InterfaceImpl::writeDimstyles(){}
void DRW_InterfaceImpl::writeAppId(){}





void ImportDXFDialog::on_checkBoxShowDetails_stateChanged(int arg1) {
	m_detailedMode = arg1;
	m_ui->groupBox->setVisible(m_detailedMode);
	m_ui->pushButtonConvert->setVisible(m_detailedMode);
	m_ui->plainTextEditLogWindow->setVisible(m_detailedMode);
	m_ui->pushButtonImport->setEnabled(!m_detailedMode);

	// adjust size
	QSize preferredSize = sizeHint();
	preferredSize.setWidth(width()); // Maintain the current width
	setFixedSize(preferredSize);
}


void ImportDXFDialog::on_checkBoxCustomOrigin_toggled(bool checked) {
	m_ui->lineEditCustomCenterX->setEnabled(checked);
	m_ui->lineEditCustomCenterY->setEnabled(checked);
}

