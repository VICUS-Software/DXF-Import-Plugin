#include "Georeferencing.h"

#include <cmath>
#include <memory>
#include <string>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <IBK_messages.h>
#include <IBK_physics.h>

#include <ogr_spatialref.h>
#include <ogr_srs_api.h>
#include <cpl_conv.h>
#include <cpl_error.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

/*! Distance of the sample points used to linearize a CRS transformation in [m]. */
const double SAMPLE_DISTANCE = 100;

/*! Suppresses GDAL error output while probing user input or file content.
	The errors are still recorded, CPLGetLastErrorMsg() reports them for the log.
*/
class QuietGDALErrors {
public:
	QuietGDALErrors()	{ CPLPushErrorHandler(CPLQuietErrorHandler); }
	~QuietGDALErrors()	{ CPLPopErrorHandler(); }
};


/*! Directory the plugin binary itself lives in, empty where it cannot be determined.
	Not the application directory - the plugin is loaded into SIM-VICUS and the GDAL files that ship
	with it sit next to the plugin, not next to the host executable.
*/
QString moduleDirectory() {
#ifdef Q_OS_WIN
	HMODULE module = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						   reinterpret_cast<LPCWSTR>(&moduleDirectory), &module) == 0 || module == nullptr)
		return QString();
	wchar_t path[MAX_PATH] = {0};
	DWORD len = GetModuleFileNameW(module, path, MAX_PATH);
	if (len == 0 || len >= MAX_PATH)
		return QString();
	return QFileInfo(QString::fromWCharArray(path, (int)len)).absolutePath();
#else
	// on Linux and macOS GDAL comes from the system package and brings its own data
	return QString();
#endif
}


/*! Makes sure PROJ can find its proj.db - without it every EPSG lookup fails and georeferencing is
	silently dead. Runs once, and only takes over when PROJ cannot help itself.
*/
void ensureProjData() {
	static bool done = false;
	if (done)
		return;
	done = true;

	QString gdalError;
	{
		QuietGDALErrors quiet;
		CPLErrorReset();
		OGRSpatialReference probe;
		if (probe.importFromEPSG(4326) == OGRERR_NONE)
			return; // PROJ found its database
		gdalError = QString::fromUtf8(CPLGetLastErrorMsg());
	}

	QString dir = moduleDirectory();
	QStringList candidates;
	if (!dir.isEmpty())
		candidates << dir + "/proj" << dir + "/share/proj" << dir;

	for (const QString & c : candidates) {
		if (!QFile::exists(c + "/proj.db"))
			continue;
		// The host could use GDAL as well, but if we got here its PROJ is broken too - pointing the
		// process at a working database can only help.
		QByteArray nativePath = QDir::toNativeSeparators(c).toUtf8();
		const char * paths[2] = { nativePath.constData(), nullptr };
		OSRSetPROJSearchPaths(paths);

		QString gdalData = QFileInfo(c).absolutePath() + "/gdal-data";
		if (QFile::exists(gdalData))
			CPLSetConfigOption("GDAL_DATA", QDir::toNativeSeparators(gdalData).toUtf8().constData());

		IBK::IBK_Message(IBK::FormatString("Using PROJ database in '%1'.").arg(c.toStdString()),
						 IBK::MSG_PROGRESS, "[Georeferencing::ensureProjData]", IBK::VL_INFO);
		return;
	}

	IBK::IBK_Message(IBK::FormatString("PROJ cannot find its database 'proj.db', coordinate reference "
									   "systems will not resolve (%1). Expected it next to the plugin "
									   "binary in '%2'.")
					 .arg(gdalError.toStdString()).arg(dir.isEmpty() ? std::string("<unknown>") : dir.toStdString()),
					 IBK::MSG_ERROR, "[Georeferencing::ensureProjData]", IBK::VL_STANDARD);
}


/*! Fills name and WKT of 'crs' from the given spatial reference. */
void storeSpatialReference(OGRSpatialReference & srs, Georeferencing::CoordinateSystem & crs) {
	srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

	char *wkt = nullptr;
	if (srs.exportToWkt(&wkt) != OGRERR_NONE || wkt == nullptr) {
		CPLFree(wkt);
		return;
	}
	crs.m_wkt = QString::fromUtf8(wkt);
	CPLFree(wkt);

	// an EPSG code is much more telling than the CRS name, so try to identify one
	if (srs.GetAuthorityCode(nullptr) == nullptr)
		srs.AutoIdentifyEPSG();

	const char *authName = srs.GetAuthorityName(nullptr);
	const char *authCode = srs.GetAuthorityCode(nullptr);
	if (authName != nullptr && authCode != nullptr)
		crs.m_name = QString("%1:%2").arg(authName).arg(authCode);
	else if (srs.GetName() != nullptr)
		crs.m_name = QString::fromUtf8(srs.GetName());
}


/*! Tries to interpret 'text' as CRS definition, also digging out an embedded EPSG code.
	Returns an invalid CoordinateSystem if the text cannot be interpreted.
*/
Georeferencing::CoordinateSystem parseDefinition(const QString & text) {
	Georeferencing::CoordinateSystem crs;
	QString definition = text.trimmed();
	if (definition.isEmpty())
		return crs;

	ensureProjData();
	QuietGDALErrors quiet;
	CPLErrorReset();

	OGRSpatialReference srs;
	if (srs.SetFromUserInput(definition.toUtf8().constData()) == OGRERR_NONE) {
		storeSpatialReference(srs, crs);
		if (crs.isValid())
			return crs;
	}

	// CAD programs write coordinate system codes of their own (for example "ETRS89.UTM-33N"), which
	// GDAL cannot resolve. Most of them still carry the EPSG code somewhere in the definition.
	// A WKT-shaped definition names several codes, and the first one is usually the datum or the
	// geographic base system - taking that would read the drawing coordinates as degrees. So walk all
	// of them and keep the last projected system; a geographic one only if nothing else resolves.
	static const QRegularExpression epsgRe("EPSG[\"'\\s:,_]*(\\d{4,6})", QRegularExpression::CaseInsensitiveOption);
	Georeferencing::CoordinateSystem geographicFallback;
	QRegularExpressionMatchIterator it = epsgRe.globalMatch(definition);
	while (it.hasNext()) {
		OGRSpatialReference epsgSrs;
		if (epsgSrs.importFromEPSG(it.next().captured(1).toInt()) != OGRERR_NONE)
			continue;

		Georeferencing::CoordinateSystem candidate;
		storeSpatialReference(epsgSrs, candidate);
		if (!candidate.isValid())
			continue;

		if (epsgSrs.IsProjected())
			crs = candidate;
		else if (!geographicFallback.isValid())
			geographicFallback = candidate;
	}

	if (!crs.isValid())
		crs = geographicFallback;

	// a definition that resolves nowhere is worth a log line - it is usually a missing proj.db
	if (!crs.isValid()) {
		// GDAL does not report a message for every rejection, only append one when there is one
		std::string gdalError = CPLGetLastErrorMsg();
		IBK::IBK_Message(IBK::FormatString("Cannot interpret coordinate reference system '%1'.%2")
						 .arg(definition.left(200).toStdString())
						 .arg(gdalError.empty() ? std::string() : " " + gdalError),
						 IBK::MSG_WARNING, "[Georeferencing::parseDefinition]", IBK::VL_STANDARD);
	}

	return crs;
}


/*! Creates a UTM spatial reference on the WGS84 datum. */
void setupUtm(OGRSpatialReference & srs, int utmZone, bool north) {
	// prefer the EPSG definition, it carries authority and name and thus survives a .prj round trip
	if (srs.importFromEPSG((north ? 32600 : 32700) + utmZone) != OGRERR_NONE) {
		srs.SetProjCS("UTM");
		srs.SetWellKnownGeogCS("WGS84");
		srs.SetUTM(utmZone, north);
	}
	srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
}

} // namespace


Georeferencing::CoordinateSystem Georeferencing::readSidecarPrj(const QString & filePath) {
	CoordinateSystem crs;

	QFileInfo finfo(filePath);
	QString prjFilePath = finfo.dir().filePath(finfo.completeBaseName() + ".prj");
	QFile prjFile(prjFilePath);
	if (!prjFile.exists() || !prjFile.open(QIODevice::ReadOnly | QIODevice::Text))
		return crs;

	QString definition = QString::fromUtf8(prjFile.readAll());
	prjFile.close();

	crs = parseDefinition(definition);
	if (crs.isValid())
		crs.m_source = tr("Sidecar file '%1'").arg(QFileInfo(prjFilePath).fileName());

	return crs;
}


Georeferencing::CoordinateSystem Georeferencing::readDxfGeoData(const QString & dxfFilePath,
																	DesignTransform & design) {
	CoordinateSystem crs;

	QFile dxfFile(dxfFilePath);
	if (!dxfFile.open(QIODevice::ReadOnly))
		return crs;

	// binary DXF files cannot be scanned line by line
	if (dxfFile.peek(18) == QByteArray("AutoCAD Binary DXF"))
		return crs;

	QTextStream in(&dxfFile);

	bool			inObjects = false;
	bool			expectSectionName = false;
	bool			inGeoData = false;
	bool			haveDesignData = false;
	QString			definition;
	DesignTransform readDesign;
	double			northX = 0;
	double			northY = 1;

	while (!in.atEnd()) {
		bool codeOk = false;
		int code = in.readLine().trimmed().toInt(&codeOk);
		if (in.atEnd())
			break;
		QString value = in.readLine().trimmed();
		if (!codeOk)
			continue;

		// AcDbGeoData lives in the OBJECTS section - skipping everything before it and stopping at its
		// end keeps a drawing without georeference data from being read end to end for nothing
		if (code == 0) {
			if (value.compare("SECTION", Qt::CaseInsensitive) == 0) {
				expectSectionName = true;
				continue;
			}
			if (value.compare("ENDSEC", Qt::CaseInsensitive) == 0) {
				if (inObjects)
					break;
				inObjects = false;
				continue;
			}
		}
		// the name of a section is the group code 2 right after its start marker
		if (expectSectionName) {
			expectSectionName = false;
			if (code == 2) {
				inObjects = (value.compare("OBJECTS", Qt::CaseInsensitive) == 0);
				continue;
			}
		}
		if (!inObjects)
			continue;

		if (code == 0) {
			if (inGeoData)
				break; // next object starts, AcDbGeoData is complete
			inGeoData = (value.compare("GEODATA", Qt::CaseInsensitive) == 0);
			continue;
		}
		if (!inGeoData)
			continue;

		switch (code) {
			// long definitions are split into several group values of the same code
			case 301 : definition += value; break;
			case  10 : readDesign.m_designPoint.m_x = value.toDouble(); haveDesignData = true; break;
			case  20 : readDesign.m_designPoint.m_y = value.toDouble(); break;
			case  11 : readDesign.m_referencePoint.m_x = value.toDouble(); haveDesignData = true; break;
			case  21 : readDesign.m_referencePoint.m_y = value.toDouble(); break;
			case  12 : northX = value.toDouble(); haveDesignData = true; break;
			case  22 : northY = value.toDouble(); break;
			case  40 : readDesign.m_scale = value.toDouble(); haveDesignData = true; break;
			default  : break;
		}
	}
	dxfFile.close();

	crs = parseDefinition(definition);
	if (!crs.isValid())
		return crs;

	crs.m_source = tr("AcDbGeoData object of the DXF file");

	if (haveDesignData) {
		// group 12 holds the direction of grid north in drawing coordinates, the drawing has to be
		// rotated such that this direction ends up along the y axis of the CRS
		if (northX != 0 || northY != 0)
			readDesign.m_rotation = 0.5*IBK::PI - std::atan2(northY, northX);
		if (readDesign.m_scale <= 0)
			readDesign.m_scale = 1;
		readDesign.m_fromGeoData = true;
		design = readDesign;
	}

	return crs;
}


Georeferencing::CoordinateSystem Georeferencing::detectCoordinateSystem(const QString & dxfFilePath,
																			DesignTransform & design) {
	// the sidecar file wins, it is what a GIS program writes next to an exported DXF
	CoordinateSystem crs = readSidecarPrj(dxfFilePath);
	if (crs.isValid())
		return crs;

	return readDxfGeoData(dxfFilePath, design);
}


Georeferencing::CoordinateSystem Georeferencing::fromUserInput(const QString & crsText) {
	QString definition = crsText.trimmed();

	// a plain number is meant as EPSG code
	static const QRegularExpression digitsOnlyRe("^\\d{4,6}$");
	if (digitsOnlyRe.match(definition).hasMatch())
		definition = "EPSG:" + definition;

	CoordinateSystem crs = parseDefinition(definition);
	if (crs.isValid())
		crs.m_source = tr("User input");

	return crs;
}


int Georeferencing::utmZone(const CoordinateSystem & crs, bool & north) {
	north = true;
	if (!crs.isValid())
		return -1;

	QuietGDALErrors quiet;

	OGRSpatialReference srs;
	if (srs.importFromWkt(crs.m_wkt.toUtf8().constData()) != OGRERR_NONE)
		return -1;

	int isNorth = 1;
	int zone = srs.GetUTMZone(&isNorth);
	if (zone == 0)
		return -1;

	north = (isNorth != 0);
	return zone;
}


Georeferencing::CoordinateSystem Georeferencing::utmSystem(int utmZone, bool north) {
	CoordinateSystem crs;
	if (utmZone < 1 || utmZone > 60)
		return crs;

	ensureProjData();
	QuietGDALErrors quiet;

	OGRSpatialReference srs;
	setupUtm(srs, utmZone, north);
	storeSpatialReference(srs, crs);
	return crs;
}


bool Georeferencing::toGeographic(const CoordinateSystem & crs, const IBKMK::Vector2D & point,
								  double & lon, double & lat) {
	if (!crs.isValid())
		return false;

	QuietGDALErrors quiet;

	OGRSpatialReference srs;
	if (srs.importFromWkt(crs.m_wkt.toUtf8().constData()) != OGRERR_NONE)
		return false;
	srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

	OGRSpatialReference wgs84;
	wgs84.SetWellKnownGeogCS("WGS84");
	wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

	lon = point.m_x;
	lat = point.m_y;

	if (!srs.IsSame(&wgs84)) {
		std::unique_ptr<OGRCoordinateTransformation> ct(OGRCreateCoordinateTransformation(&srs, &wgs84));
		if (ct == nullptr || !ct->Transform(1, &lon, &lat))
			return false;
	}

	return (lon >= -180 && lon <= 180 && lat >= -90 && lat <= 90);
}


int Georeferencing::utmZoneForPoint(const CoordinateSystem & crs, const IBKMK::Vector2D & point, bool & north) {
	north = true;

	double lon = 0, lat = 0;
	if (!toGeographic(crs, point, lon, lat))
		return -1;

	north = (lat >= 0);
	int zone = (int)std::floor((lon + 180.0)/6.0) + 1;
	if (zone < 1)   zone = 1;
	if (zone > 60)  zone = 60;
	return zone;
}


IBKMK::Vector2D Georeferencing::drawingToCRS(const DesignTransform & design, const IBKMK::Vector2D & point) {
	double cosRho = std::cos(design.m_rotation);
	double sinRho = std::sin(design.m_rotation);
	IBKMK::Vector2D d = point - design.m_designPoint;
	return IBKMK::Vector2D(design.m_referencePoint.m_x + design.m_scale*(cosRho*d.m_x - sinRho*d.m_y),
						   design.m_referencePoint.m_y + design.m_scale*(sinRho*d.m_x + cosRho*d.m_y));
}


bool Georeferencing::computePlacement(const CoordinateSystem & source, const DesignTransform & design,
										int utmZone, bool north, const IBKMK::Vector2D & referencePoint,
										Placement & placement, QString & errmsg) {
	if (!source.isValid()) {
		errmsg = tr("No coordinate reference system given.");
		return false;
	}
	if (utmZone < 1 || utmZone > 60) {
		errmsg = tr("Invalid UTM zone %1, expected a value between 1 and 60.").arg(utmZone);
		return false;
	}
	if (design.m_scale <= 0) {
		errmsg = tr("Invalid drawing unit scaling factor.");
		return false;
	}

	ensureProjData();
	QuietGDALErrors quiet;

	OGRSpatialReference sourceSrs;
	if (sourceSrs.importFromWkt(source.m_wkt.toUtf8().constData()) != OGRERR_NONE) {
		errmsg = tr("Cannot interpret the coordinate reference system of the drawing.");
		return false;
	}
	sourceSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

	OGRSpatialReference targetSrs;
	setupUtm(targetSrs, utmZone, north);

	// reference point in coordinates of the source CRS
	IBKMK::Vector2D q0 = drawingToCRS(design, referencePoint);

	double theta = 0;
	double k = 1;
	IBKMK::Vector2D utmReference = q0;

	if (!sourceSrs.IsSame(&targetSrs)) {
		std::unique_ptr<OGRCoordinateTransformation> ct(OGRCreateCoordinateTransformation(&sourceSrs, &targetSrs));
		if (ct == nullptr) {
			errmsg = tr("Cannot transform from '%1' to UTM zone %2.").arg(source.m_name).arg(utmZone);
			return false;
		}

		// linearize the transformation around the reference point
		double x[3] = { q0.m_x, q0.m_x + SAMPLE_DISTANCE, q0.m_x };
		double y[3] = { q0.m_y, q0.m_y, q0.m_y + SAMPLE_DISTANCE };
		if (!ct->Transform(3, x, y)) {
			errmsg = tr("Cannot transform the drawing coordinates to UTM zone %1. "
						"Please check the selected coordinate reference system.").arg(utmZone);
			return false;
		}

		utmReference.set(x[0], y[0]);
		IBKMK::Vector2D ex((x[1] - x[0])/SAMPLE_DISTANCE, (y[1] - y[0])/SAMPLE_DISTANCE);
		IBKMK::Vector2D ey((x[2] - x[0])/SAMPLE_DISTANCE, (y[2] - y[0])/SAMPLE_DISTANCE);

		// a mirrored transformation cannot be expressed as drawing placement
		if (ex.m_x*ey.m_y - ex.m_y*ey.m_x <= 0) {
			errmsg = tr("The coordinate reference system '%1' mirrors the drawing, which is not supported.")
					 .arg(source.m_name);
			return false;
		}

		k = 0.5*(ex.magnitude() + ey.magnitude());
		if (k <= 0) {
			errmsg = tr("Degenerated transformation from '%1' to UTM zone %2.").arg(source.m_name).arg(utmZone);
			return false;
		}
		theta = std::atan2(ex.m_y, ex.m_x);
	}

	placement.m_referenceUtm = utmReference;
	placement.m_gridScale = k;
	placement.m_convergence = theta;
	placement.m_rotation = theta + design.m_rotation;
	placement.m_scale = k*design.m_scale;

	// translation such that the reference point ends up at its UTM coordinate
	double cosPhi = std::cos(placement.m_rotation);
	double sinPhi = std::sin(placement.m_rotation);
	placement.m_translation.set(
		utmReference.m_x - placement.m_scale*(cosPhi*referencePoint.m_x - sinPhi*referencePoint.m_y),
		utmReference.m_y - placement.m_scale*(sinPhi*referencePoint.m_x + cosPhi*referencePoint.m_y));

	return true;
}


QString Georeferencing::utmName(int utmZone, bool north) {
	// WGS84 / UTM zone NN N is EPSG:326NN, the southern zones are EPSG:327NN
	return QString("EPSG:%1").arg((north ? 32600 : 32700) + utmZone);
}
