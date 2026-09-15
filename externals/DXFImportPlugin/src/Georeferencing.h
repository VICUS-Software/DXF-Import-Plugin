#ifndef GeoreferencingH
#define GeoreferencingH

#include <QCoreApplication>
#include <QString>

#include <IBKMK_Vector2D.h>

/*! Reads the GIS coordinate reference system (CRS) of a DXF file and maps such a CRS onto the UTM
	world coordinate system of a VICUS project.

	DXF has no mandatory place for a CRS, so two conventions are supported:
	- an ESRI style sidecar file <baseName>.prj next to the DXF (written by QGIS and ArcGIS)
	- an AcDbGeoData object inside the DXF (written by AutoCAD and BricsCAD)

	libdxfrw does not parse the OBJECTS section, so the AcDbGeoData object is read with a raw group
	code scan over the file.
*/
class Georeferencing {
	Q_DECLARE_TR_FUNCTIONS(Georeferencing)
public:

	/*! Coordinate reference system, detected from a file or entered by the user. */
	struct CoordinateSystem {
		/*! Returns true if a CRS definition is available. */
		bool isValid() const { return !m_wkt.isEmpty(); }

		/*! CRS definition in WKT notation. */
		QString			m_wkt;
		/*! Human readable name, "EPSG:25833" if an authority code is known, otherwise the CRS name. */
		QString			m_name;
		/*! Where the definition was taken from, shown to the user (translated). */
		QString			m_source;
	};

	/*! Mapping from DXF drawing coordinates (WCS) to coordinates of the CRS, as stored in an
		AcDbGeoData object:  crs = m_referencePoint + R(m_rotation) * m_scale * (wcs - m_designPoint).

		Without an AcDbGeoData object the drawing coordinates are the CRS coordinates already and only
		m_scale is needed to convert drawing units to meters.
	*/
	struct DesignTransform {
		/*! Point in drawing coordinates. */
		IBKMK::Vector2D	m_designPoint;
		/*! The same point in CRS coordinates in [m]. */
		IBKMK::Vector2D	m_referencePoint;
		/*! Rotation from drawing coordinates to CRS in [rad], positive counter-clockwise. */
		double			m_rotation = 0;
		/*! Scaling factor from drawing units to [m]. */
		double			m_scale = 1;
		/*! True if the values were read from an AcDbGeoData object, false for the identity mapping. */
		bool			m_fromGeoData = false;
	};

	/*! Rigid placement of a DXF drawing in the UTM system of the project:
		utm = m_translation + R(m_rotation) * m_scale * wcs.

		A drawing is placed rigidly (see Drawing), so the CRS transformation is linearized around a
		reference point inside the drawing. For district sized drawings the error of that
		linearization stays well below a millimeter.
	*/
	struct Placement {
		/*! Rotation about the z axis in [rad], positive counter-clockwise. */
		double			m_rotation = 0;
		/*! Scaling factor from drawing units to [m]. */
		double			m_scale = 1;
		/*! Translation in UTM coordinates in [m]. */
		IBKMK::Vector2D	m_translation;
		/*! UTM coordinate of the reference point the transformation was linearized around, in [m]. */
		IBKMK::Vector2D	m_referenceUtm;
		/*! Grid scale ratio between source CRS and UTM [-], 1 if both are the same CRS. */
		double			m_gridScale = 1;
		/*! Meridian convergence between source CRS and UTM in [rad], 0 if both are the same CRS. */
		double			m_convergence = 0;
	};

	/*! Maps a point from drawing coordinates to coordinates of the CRS. */
	static IBKMK::Vector2D drawingToCRS(const DesignTransform & design, const IBKMK::Vector2D & point);

	/*! Reads the CRS from an ESRI sidecar file <baseName>.prj next to 'filePath'.
		Returns an invalid CoordinateSystem if there is no such file or it cannot be interpreted.
	*/
	static CoordinateSystem readSidecarPrj(const QString & filePath);

	/*! Reads CRS and design transformation from the AcDbGeoData object of an ASCII DXF file.
		Returns an invalid CoordinateSystem if the file holds no georeference data. 'design' is only
		modified when the DXF defines a design transformation.
	*/
	static CoordinateSystem readDxfGeoData(const QString & dxfFilePath, DesignTransform & design);

	/*! Detects the CRS of a DXF file, sidecar file first, then AcDbGeoData.
		'design' is only modified when the DXF defines a design transformation.
	*/
	static CoordinateSystem detectCoordinateSystem(const QString & dxfFilePath, DesignTransform & design);

	/*! Builds a CRS from user input, accepts "EPSG:25833", "25833", WKT and PROJ strings.
		Returns an invalid CoordinateSystem if the text cannot be interpreted.
	*/
	static CoordinateSystem fromUserInput(const QString & crsText);

	/*! Returns the UTM zone stored in the given CRS, or -1 if it is not a UTM system. */
	static int utmZone(const CoordinateSystem & crs, bool & north);

	/*! Returns the UTM zone the given point falls into.
		\param point Point in CRS coordinates of 'crs'.
		\param north Receives the hemisphere.
		Returns -1 if the point cannot be converted to geographic coordinates.
	*/
	static int utmZoneForPoint(const CoordinateSystem & crs, const IBKMK::Vector2D & point, bool & north);

	/*! Computes the placement of a DXF drawing in the UTM system of the project.
		\param source CRS the DXF coordinates are given in.
		\param design Mapping from drawing coordinates to CRS coordinates, identity mapping with the
			   drawing unit scale if the DXF holds no AcDbGeoData object.
		\param referencePoint Point in drawing coordinates the transformation is linearized around,
			   should lie inside the drawing.
		\param errmsg Receives a translated error message if the function returns false.
		\note Elevations are passed through unchanged, no vertical datum transformation is done.
	*/
	static bool computePlacement(const CoordinateSystem & source, const DesignTransform & design,
								 int utmZone, bool north, const IBKMK::Vector2D & referencePoint,
								 Placement & placement, QString & errmsg);

	/*! Returns "EPSG:32633" style name of a UTM coordinate system on the WGS84 datum. */
	static QString utmName(int utmZone, bool north);
};

#endif // GeoreferencingH
