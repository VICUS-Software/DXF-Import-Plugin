#ifndef IMPORTDXFDIALOG_H
#define IMPORTDXFDIALOG_H

#include "Drawing.h"
#include "Georeferencing.h"

#include <QDialog>

#include <IBKMK_Vector2D.h>
#include <IBKMK_Vector3D.h>
#include <IBK_Line.h>

#include <libdxfrw.h>
#include <drw_interface.h>
#include <drw_objects.h>
#include <drw_base.h>


namespace Ui {
class ImportDXFDialog;
}

class ImportDXFDialog : public QDialog
{
	Q_OBJECT

public:
	ImportDXFDialog(QWidget *parent = nullptr);

	enum ImportResults {
		AddDrawings,
		ImportCancelled
	};

	enum ScaleUnit {
		SU_Auto,
		SU_Meter,
		SU_Decimeter,
		SU_Centimeter,
		SU_Millimeter,
		NUM_SU
	};

	ImportResults importFile(const QString & fname);

	~ImportDXFDialog();

	const Drawing &drawing() const;

	/*! Tells the dialog about the world coordinate origin of the project that the drawing is
		imported into. Without this the drawing defines the origin itself (see worldOrigin()).
	*/
	void setProjectWorldOrigin(const IBKMK::Vector3D & origin, int utmZone, bool north);

	/*! True if the drawing was placed through its coordinate reference system. */
	bool isGeoreferenced() const { return m_georeferenced; }

	/*! True if the drawing defines the world coordinate origin, i.e. it was georeferenced and the
		project did not have an origin, yet.
	*/
	bool proposesWorldOrigin() const { return m_georeferenced && !m_haveProjectOrigin; }

	/*! World coordinate origin in UTM coordinates in [m], only meaningful if isGeoreferenced(). */
	const IBKMK::Vector3D & worldOrigin() const { return m_worldOrigin; }
	/*! UTM zone of the world coordinate origin. */
	int worldUtmZone() const { return m_worldUtmZone; }
	/*! Hemisphere of the world coordinate origin. */
	bool worldNorth() const { return m_worldNorth; }

	static IBKMK::Vector3D boundingBox(const Drawing * drawing,
									   IBKMK::Vector3D &center,
									   bool transformPoints, const double scalingFactor);

private slots:
	void on_comboBoxUnit_activated(int index);

	void on_pushButtonConvert_clicked();

	void on_pushButtonImport_clicked();

	void on_lineEditCustomCenterX_editingFinished();

	void on_lineEditCustomCenterY_editingFinished();

	void on_groupBoxCustomCenter_clicked();

	void on_checkBoxMove_clicked(bool checked);

	void on_checkBoxCustomOrigin_stateChanged(int arg1);

	void on_checkBoxShowDetails_stateChanged(int arg1);

	void on_checkBoxCustomOrigin_toggled(bool checked);

	void on_checkBoxGeoreference_toggled(bool checked);

	void on_lineEditCRS_editingFinished();

private:
	/*! Read a specified dxf file.
		\param drawing VICUS Drawing, where all primitives are added
		\param fname Filename that will be read
		\returns true if reading has been successful
	*/
	bool readDxfFile(Drawing & drawing, const QString &fname);

	/*! Fix too big fonts. */
	void fixFonts();

	void updateImportButtonEnabledState();

	/*! Looks for a coordinate reference system of the DXF file and updates the georeferencing widgets. */
	void detectGeoreferencing();

	/*! Enables/disables the placement widgets that georeferencing takes over. */
	void updateGeoreferenceControls();

	/*! Writes a message into the georeferencing info label. */
	void setGeoreferenceInfo(const QString & text, bool warning);

	/*! Places the drawing in the UTM system of the project, sets offset, rotation and scaling factor.
		\param log Receives log messages shown in the log window.
		Returns false if no placement could be computed, the caller then falls back to the
		unreferenced placement.
	*/
	bool applyGeoreferencing(QString & log);

	/*! Returns the center of the raw drawing coordinates, used as reference point for georeferencing.
		Objects of a block are stored in block local coordinates and are skipped.
	*/
	static IBKMK::Vector2D referencePoint(const Drawing & drawing);

	/*! Pointer to UI. */
	Ui::ImportDXFDialog		*m_ui;

	/*! Last file path. */
	QString					m_filePath;

	/*! VICUS Drawing with all drawing primitives. */
	Drawing					m_drawing;

	/*! Next VICUS project ID. */
	unsigned int			m_nextId;

	/*! Return code. */
	ImportResults			m_returnCode;

	bool					m_detailedMode = false;

	/*! Dxf Scaling factor from "$INSUNIT". */
	double					m_dxfScalingFactor = 1.0;

	/*! Dxf Scaling factor from "$INSUNIT". */
	std::string				m_dxfScalingUnit = "";

	/*! Coordinate reference system of the DXF file, detected or entered by the user. */
	Georeferencing::CoordinateSystem	m_coordinateSystem;

	/*! Mapping from drawing coordinates to CRS coordinates, from an AcDbGeoData object. */
	Georeferencing::DesignTransform	m_designTransform;

	/*! True if the drawing was placed through its coordinate reference system. */
	bool					m_georeferenced = false;

	/*! True if the project already has a world coordinate origin, which must not be moved. */
	bool					m_haveProjectOrigin = false;

	/*! World coordinate origin in UTM coordinates in [m]. */
	IBKMK::Vector3D			m_worldOrigin;

	/*! UTM zone of the world coordinate origin. */
	int						m_worldUtmZone = 32;

	/*! Hemisphere of the world coordinate origin. */
	bool					m_worldNorth = true;

};

/* Implementation of DRW_Interface. A dxf file will be read from top to bottom,
 * every time an object is found, one of the following methods are called.
The objects are then inserted into drawing */

class DRW_InterfaceImpl : public DRW_Interface {

	Drawing				*m_drawing		= nullptr;

	Drawing::Block		*m_activeBlock	= nullptr;

	unsigned int		*m_nextId		= nullptr;

	bool				m_emptyLayerExists = false;

	double				*m_dxfScalingFactor = nullptr;

	std::string			*m_dxfScalingUnit = nullptr;

public :

	/*! C'tor */
	DRW_InterfaceImpl(Drawing *drawing, double *dxfScalingFactor,
					  std::string *dxfScalingUnit, unsigned int &nextId);

	/** Called when header is parsed.  */
	void addHeader(const DRW_Header* data) override;

	/** Called for every line Type.  */
	void addLType(const DRW_LType& data) override;

	/** Called for every layer. */
	void addLayer(const DRW_Layer& data) override;

	/** Called for every dim style. */
	void addDimStyle(const DRW_Dimstyle& data) override;

	/** Called for every VPORT table. */
	void addVport(const DRW_Vport& data) override;

	/** Called for every text style. */
	void addTextStyle(const DRW_Textstyle& data) override;

	/** Called for every AppId entry. */
	void addAppId(const DRW_AppId& data) override;

	/**
	 * Called for every block. Note: all entities added after this
	 * command go into this block until endBlock() is called.
	 *
	 * @see endBlock()
	 */
	void addBlock(const DRW_Block& data) override;

	/**
	 * In DWG called when the following entities corresponding to a
	 * block different from the current. Note: all entities added after this
	 * command go into this block until setBlock() is called already.
	 *
	 * int handle are the value of DRW_Block::handleBlock added with addBlock()
	 */
	void setBlock(const int handle) override;

	/** Called to end the current block */
	void endBlock() override;

	/** Called for every point */
	void addPoint(const DRW_Point& data) override;

	/** Called for every line */
	void addLine(const DRW_Line& data) override;

	/** Called for every ray */
	void addRay(const DRW_Ray& data) override;

	/** Called for every xline */
	void addXline(const DRW_Xline& data) override;

	/** Called for every arc */
	void addArc(const DRW_Arc& data) override;

	/** Called for every circle */
	void addCircle(const DRW_Circle& data) override;

	/** Called for every ellipse */
	void addEllipse(const DRW_Ellipse& data) override;

	/** Called for every lwpolyline */
	void addLWPolyline(const DRW_LWPolyline& data) override;

	/** Called for every polyline start */
	void addPolyline(const DRW_Polyline& data) override;

	/** Called for every spline */
	void addSpline(const DRW_Spline* data) override;

	/** Called for every spline knot value */
	void addKnot(const DRW_Entity& data) override;

	/** Called for every insert. */
	void addInsert(const DRW_Insert& data) override;

	/** Called for every trace start */
	void addTrace(const DRW_Trace& data) override;

	/** Called for every 3dface start */
	void add3dFace(const DRW_3Dface&) override;

	/** Called for every solid start */
	void addSolid(const DRW_Solid& data) override;

	/** Called for every Multi Text entity. */
	void addMText(const DRW_MText& data) override;

	/** Called for every Text entity. */
	void addText(const DRW_Text& data) override;

	/** Called for every aligned dimension entity. */
	void addDimAlign(const DRW_DimAligned *data) override;

	/** Called for every linear or rotated dimension entity. */
	void addDimLinear(const DRW_DimLinear *data) override;

	/** Called for every radial dimension entity. */
	void addDimRadial(const DRW_DimRadial *data) override;

	/** Called for every diametric dimension entity. */
	void addDimDiametric(const DRW_DimDiametric *data) override;

	/** Called for every angular dimension (2 lines version) entity. */
	void addDimAngular(const DRW_DimAngular *data) override;

	/** Called for every angular dimension (3 points version) entity. */
	void addDimAngular3P(const DRW_DimAngular3p *data) override;

	/** Called for every ordinate dimension entity. */
	void addDimOrdinate(const DRW_DimOrdinate *data) override;

	/** Called for every leader start. */
	void addLeader(const DRW_Leader *data) override;

	/** Called for every hatch entity. */
	void addHatch(const DRW_Hatch *data) override;

	/** Called for every viewport entity. */
	void addViewport(const DRW_Viewport&) override;

	/** Called for every image entity. */
	void addImage(const DRW_Image *data) override;

	/** Called for every image definition. */
	void linkImage(const DRW_ImageDef *data) override;

	/** Called for every comment in the DXF file (code 999). */
	void addComment(const char* comment) override;

	void writeHeader(DRW_Header& data) override;
	void writeBlocks() override;
	void writeBlockRecords() override;
	void writeEntities() override;
	void writeLTypes() override;
	void writeLayers() override;
	void writeTextstyles() override;
	void writeVports() override;
	void writeDimstyles() override;
	void writeAppId() override;

};

#endif // IMPORTDXFDIALOG_H
