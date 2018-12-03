#include "robodk_api.h"
#include <QtNetwork/QTcpSocket>
#include <QtCore/QProcess>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
// Default path on Windows:
#define ROBODK_DEFAULT_PATH_BIN "C:/RoboDK/bin/RoboDK.exe"

#elif __APPLE__
// Default Install Path on Mac
#define ROBODK_DEFAULT_PATH_BIN "~/RoboDK/Applications/RoboDK.app/Contents/MacOS/RoboDK"

#else

// Default Install Path on Linux:
#define ROBODK_DEFAULT_PATH_BIN "~/RoboDK/bin/RoboDK"

#endif

#define ROBODK_DEFAULT_PORT 20500

#define ROBODK_API_TIMEOUT 1000 // communication timeout. Raise this value for slow computers
#define ROBODK_API_START_STRING "CMD_START"
#define ROBODK_API_READY_STRING "READY"
#define ROBODK_API_LF "\n"



#define M_PI 3.14159265358979323846264338327950288


#ifndef RDK_SKIP_NAMESPACE
namespace RoboDK_API {
#endif


//--------     Generic matrix usage    ---------------
/// <summary>
/// Return a translation matrix
///                 |  1   0   0   X |
/// transl(X,Y,Z) = |  0   1   0   Y |
///                 |  0   0   1   Z |
///                 |  0   0   0   1 |
/// </summary>
/// <param name="x">translation along X (mm)</param>
/// <param name="y">translation along Y (mm)</param>
/// <param name="z">translation along Z (mm)</param>
/// <returns></returns>
Mat transl(double x, double y, double z){
    Mat mat;
    mat.setToIdentity();
    mat.setPos(x, y, z);
    return mat;
}

/// <summary>
/// Return a X-axis rotation matrix
///            |  1  0        0        0 |
/// rotx(rx) = |  0  cos(rx) -sin(rx)  0 |
///            |  0  sin(rx)  cos(rx)  0 |
///            |  0  0        0        1 |
/// </summary>
/// <param name="rx">rotation around X axis (in radians)</param>
/// <returns></returns>
Mat rotx(double rx){
    double cx = cos(rx);
    double sx = sin(rx);
    return Mat(1, 0, 0, 0, 0, cx, -sx, 0, 0, sx, cx, 0);
}

/// <summary>
/// Return a Y-axis rotation matrix
///            |  cos(ry)  0   sin(ry)  0 |
/// roty(ry) = |  0        1   0        0 |
///            | -sin(ry)  0   cos(ry)  0 |
///            |  0        0   0        1 |
/// </summary>
/// <param name="ry">rotation around Y axis (in radians)</param>
/// <returns></returns>
Mat roty(double ry){
    double cy = cos(ry);
    double sy = sin(ry);
    return Mat(cy, 0, sy, 0, 0, 1, 0, 0, -sy, 0, cy, 0);
}

/// <summary>
/// Return a Z-axis rotation matrix
///            |  cos(rz)  -sin(rz)   0   0 |
/// rotz(rx) = |  sin(rz)   cos(rz)   0   0 |
///            |  0         0         1   0 |
///            |  0         0         0   1 |
/// </summary>
/// <param name="rz">rotation around Z axis (in radians)</param>
/// <returns></returns>
Mat rotz(double rz){
    double cz = cos(rz);
    double sz = sin(rz);
    return Mat(cz, -sz, 0, 0, sz, cz, 0, 0, 0, 0, 1, 0);
}


//----------------------------------------------------
//------ Pose to xyzrpw and xyzrpw to pose------------
/// <summary>
/// Calculates the equivalent position and euler angles ([x,y,z,r,p,w] vector) of the given pose
/// Note: transl(x,y,z)*rotz(w*pi/180)*roty(p*pi/180)*rotx(r*pi/180)
/// See also: FromXYZRPW()
/// </summary>
/// <returns>XYZWPR translation and rotation in mm and degrees</returns>
void Mat::ToXYZRPW(tXYZWPR xyzwpr){
    double x = Get(0,3);
    double y = Get(1,3);
    double z = Get(2,3);
    double w, p, r;
    if (Get(2,0) > (1.0 - 1e-6)){
        p = -M_PI*0.5;
        r = 0;
        w = atan2(-Get(1,2), Get(1,1));
    } else if (Get(2,0) < -1.0 + 1e-6){
        p = 0.5*M_PI;
        r = 0;
        w = atan2(Get(1,2),Get(1,1));
    } else {
        p = atan2(-Get(2, 0), sqrt(Get(0, 0) * Get(0, 0) + Get(1, 0) * Get(1, 0)));
        w = atan2(Get(1, 0), Get(0, 0));
        r = atan2(Get(2, 1), Get(2, 2));
    }
    xyzwpr[0] = x;
    xyzwpr[1] = y;
    xyzwpr[2] = z;
    xyzwpr[3] = r*180.0/M_PI;
    xyzwpr[4] = p*180.0/M_PI;
    xyzwpr[5] = w*180.0/M_PI;
}

QString Mat::ToString(const QString &separator, int precision, bool in_xyzwpr){
    tXYZWPR xyzwpr;
    ToXYZRPW(xyzwpr);

    QString str;
    str.append(QString::number(xyzwpr[0],'f',precision));
    for (int i=1; i<6; i++){
        str.append(separator);
        str.append(QString::number(xyzwpr[i],'f',precision));
    }
    return str;
}

bool Mat::FromString(const QString &pose_str){
    QStringList values_list = QString(pose_str).replace(";",",").replace("\t",",").split(",", QString::SkipEmptyParts);
    int nvalues = qMin(values_list.length(), 6);
    tXYZWPR xyzwpr;
    for (int i=0; i<6; i++){
        xyzwpr[i] = 0.0;
    }
    if (nvalues < 6){
        FromXYZRPW(xyzwpr);
        return false;
    }
    for (int i=0; i<nvalues; i++){
        QString stri(values_list.at(i));
        xyzwpr[i] = stri.trimmed().toDouble();
    }
    FromXYZRPW(xyzwpr);
    return true;
}


//----------------------------------- Joints class ------------------------
tJoints::tJoints(int ndofs){
    nDOFs = qMin(ndofs, RDK_SIZE_JOINTS_MAX);
    for (int i=0; i<nDOFs; i++){
        Values[i] = 0.0;
    }
}
tJoints::tJoints(const tJoints &copy){
    SetValues(copy.Values, copy.nDOFs);
}
tJoints::tJoints(const tMatrix2D *mat2d, int column, int ndofs){
    if (Matrix2D_Size(mat2d, 2) >= column){
        nDOFs = 0;
        qDebug()<<"Warning: tMatrix2D column outside range when creating joints";
    }
    if (ndofs < 0){
        ndofs = Matrix2D_Size(mat2d, 1);
    }
    nDOFs = qMin(ndofs, RDK_SIZE_JOINTS_MAX);

    double *ptr = Matrix2D_Get_col(mat2d, column);
    SetValues(ptr, nDOFs);
}
tJoints::tJoints(const QString &str){
    nDOFs = 0;
    FromString(str);
}

void tJoints::SetValues(const double *values, int ndofs){
    if (ndofs >= 0){
        nDOFs = ndofs;
    }
    for (int i=0; i<nDOFs; i++){
        Values[i] = values[i];
    }
}
int tJoints::GetValues(double *values){
    for (int i=0; i<nDOFs; i++){
        values[i] = Values[i];
    }
    return nDOFs;
}
QString tJoints::ToString(const QString &separator, int precision){
    QString values;
    if (nDOFs <= 0){
        return values;
    }
    values.append(QString::number(Values[0],'f',precision));
    for (int i=1; i<nDOFs; i++){
        values.append(separator);
        values.append(QString::number(Values[i],'f',precision));
    }
    return values;
}
bool tJoints::FromString(const QString &str){
    QStringList jnts_list = QString(str).replace(";",",").replace("\t",",").split(",", QString::SkipEmptyParts);
    nDOFs = qMin(jnts_list.length(), RDK_SIZE_JOINTS_MAX);
    for (int i=0; i<nDOFs; i++){
        QString stri(jnts_list.at(i));
        Values[i] = stri.trimmed().toDouble();
    }
    return true;
}
double* tJoints::Data(){
    return Values;
}
int tJoints::Length(){
    return nDOFs;
}
//---------------------------------------------------------------------





/// <summary>
/// Calculates the pose from the position and euler angles ([x,y,z,r,p,w] vector)
/// The result is the same as calling: H = transl(x,y,z)*rotz(w*pi/180)*roty(p*pi/180)*rotx(r*pi/180)
/// </summary>
/// <param name="x"></param>
/// <param name="y"></param>
/// <param name="z"></param>
/// <param name="w"></param>
/// <param name="p"></param>
/// <param name="r"></param>
/// <returns>Homogeneous matrix (4x4)</returns>
Mat Mat::XYZRPW_2_Mat(double x, double y, double z, double r, double p, double w){
    double a = r * M_PI / 180.0;
    double b = p * M_PI / 180.0;
    double c = w * M_PI / 180.0;
    double ca = cos(a);
    double sa = sin(a);
    double cb = cos(b);
    double sb = sin(b);
    double cc = cos(c);
    double sc = sin(c);
    return Mat(cb * cc, cc * sa * sb - ca * sc, sa * sc + ca * cc * sb, x, cb * sc, ca * cc + sa * sb * sc, ca * sb * sc - cc * sa, y, -sb, cb * sa, ca * cb, z);
}
Mat Mat::XYZRPW_2_Mat(tXYZWPR xyzwpr){
    return XYZRPW_2_Mat(xyzwpr[0], xyzwpr[1], xyzwpr[2], xyzwpr[3], xyzwpr[4], xyzwpr[5]);
}

void Mat::FromXYZRPW(tXYZWPR xyzwpr){
    Mat newmat = Mat::XYZRPW_2_Mat(xyzwpr[0], xyzwpr[1], xyzwpr[2], xyzwpr[3], xyzwpr[4], xyzwpr[5]);
    for (int i=0; i<4; i++){
        for (int j=0; j<4; j++){
            this->Set(i,j, newmat.Get(i,j));
        }
    }
}





Mat::Mat() : QMatrix4x4() {
    setToIdentity();
}

Mat::Mat(const QMatrix4x4 &matrix) : QMatrix4x4(matrix) {
    // just copy
}

Mat::Mat(double nx, double ox, double ax, double tx, double ny, double oy, double ay, double ty, double nz, double oz, double az, double tz) :
    QMatrix4x4(nx, ox, ax, tx, ny, oy, ay, ty, nz, oz, az, tz, 0, 0, 0, 1)
{

}
Mat::~Mat(){

}
void Mat::setVX(double x, double y, double z){
    Set(0,0, x);
    Set(1,0, y);
    Set(2,0, z);
}
void Mat::VX(tXYZ xyz){
    xyz[0] = Get(0, 0);
    xyz[1] = Get(1, 0);
    xyz[2] = Get(2, 0);
}

void Mat::setVY(double x, double y, double z){
    Set(0,1, x);
    Set(1,1, y);
    Set(2,1, z);
}
void Mat::VY(tXYZ xyz){
    xyz[0] = Get(0, 1);
    xyz[1] = Get(1, 1);
    xyz[2] = Get(2, 1);
}

void Mat::setVZ(double x, double y, double z){
    Set(0,2, x);
    Set(1,2, y);
    Set(2,2, z);
}
void Mat::VZ(tXYZ xyz){
    xyz[0] = Get(0, 2);
    xyz[1] = Get(1, 2);
    xyz[2] = Get(2, 2);
}

void Mat::setPos(double x, double y, double z){
    Set(0,3, x);
    Set(1,3, y);
    Set(2,3, z);
}
void Mat::Pos(tXYZ xyz){
    xyz[0] = Get(0, 3);
    xyz[1] = Get(1, 3);
    xyz[2] = Get(2, 3);
}

Mat Mat::inv() const{
    return this->inverted();
}


void Mat::Set(int i, int j, double value){
    QVector4D rw(this->row(i));
    rw[j] = value;
    setRow(i, rw);
    // the following should not crash!!
    //float **dt_ok = (float**) data();
    //dt_ok[i][j] = value;
}
double Mat::Get(int i, int j) const{
    return row(i)[j];
    // the following hsould be allowed!!
    //return ((const float**)data())[i][j];
}

//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
/////////////////////////////////// Item CLASS ////////////////////////////////////////////////////
Item::Item(RoboDK *rdk, quint64 ptr, qint32 type) {
    _RDK = rdk;
    _PTR = ptr;
    _TYPE = type;
}
Item::Item(const Item &other) {
    _RDK = other._RDK;
    _PTR = other._PTR;
    _TYPE = other._TYPE;
}
Item::~Item(){

}



/// <summary>
/// Returns the RoboDK link Robolink().
/// </summary>
/// <returns></returns>
RoboDK* Item::RDK(){
    return _RDK;
}

/// <summary>
/// Create a new communication link for RoboDK. Use this for robots if you use a multithread application running multiple robots at the same time.
/// </summary>
void Item::NewLink(){
    _RDK = new RoboDK();
}

//////// GENERIC ITEM CALLS
/// <summary>
/// Returns the type of an item (robot, object, target, reference frame, ...)
/// </summary>
/// <returns></returns>
int Item::Type(){
    _RDK->_check_connection();
    _RDK->_send_Line("G_Item_Type");
    _RDK->_send_Item(this);
    int itemtype = _RDK->_recv_Int();
    _RDK->_check_status();
    return itemtype;
}

////// add more methods

/// <summary>
/// Save a station or object to a file
/// </summary>
/// <param name="filename"></param>
void Item::Save(const QString &filename){
    _RDK->Save(filename, this);
}

/// <summary>
/// Deletes an item and its childs from the station.
/// </summary>
void Item::Delete(){
    _RDK->_check_connection();
    _RDK->_send_Line("Remove");
    _RDK->_send_Item(this);
    _RDK->_check_status();
    _PTR = 0;
    _TYPE = -1;
}

/// <summary>
/// Checks if the item is valid. An invalid item will be returned by an unsuccessful function call.
/// </summary>
/// <returns>true if valid, false if invalid</returns>
bool Item::Valid(){
    return _PTR != NULL;
}
/// <summary>
/// Attaches the item to a new parent while maintaining the relative position with its parent. The absolute position is changed.
/// </summary>
/// <param name="parent"></param>
void Item::setParent(Item parent){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Parent");
    _RDK->_send_Item(this);
    _RDK->_send_Item(parent);
    _RDK->_check_status();
}

/// <summary>
/// Attaches the item to another parent while maintaining the current absolute position in the station.
/// The relationship between this item and its parent is changed to maintain the abosolute position.
/// </summary>
/// <param name="parent">parent item to attach this item</param>
void Item::setParentStatic(Item parent)
{
    _RDK->_check_connection();
    _RDK->_send_Line("S_Parent_Static");
    _RDK->_send_Item(this);
    _RDK->_send_Item(parent);
    _RDK->_check_status();
}


////// add more methods
/// <summary>
/// Returns a list of the item childs that are attached to the provided item.
/// </summary>
/// <returns>item x n -> list of child items</returns>
QList<Item> Item::Childs(){
    _RDK->_check_connection();
    _RDK->_send_Line("G_Childs");
    _RDK->_send_Item(this);
    int nitems = _RDK->_recv_Int();
    QList<Item> itemlist;
    for (int i = 0; i < nitems; i++)
    {
        itemlist.append(_RDK->_recv_Item());
    }
    _RDK->_check_status();
    return itemlist;
}

/// <summary>
/// Returns 1 if the item is visible, otherwise, returns 0.
/// </summary>
/// <returns>true if visible, false if not visible</returns>
bool Item::Visible(){
    _RDK->_check_connection();
    _RDK->_send_Line("G_Visible");
    _RDK->_send_Item(this);
    int visible = _RDK->_recv_Int();
    _RDK->_check_status();
    return (visible != 0);
}
/// <summary>
/// Sets the item visiblity status
/// </summary>
/// <param name="visible"></param>
/// <param name="visible_frame">srt the visible reference frame (1) or not visible (0)</param>
void Item::setVisible(bool visible, int visible_frame){
    if (visible_frame < 0)
    {
        visible_frame = visible ? 1 : 0;
    }
    _RDK->_check_connection();
    _RDK->_send_Line("S_Visible");
    _RDK->_send_Item(this);
    _RDK->_send_Int(visible ? 1 : 0);
    _RDK->_send_Int(visible_frame);
    _RDK->_check_status();
}

/// <summary>
/// Returns the name of an item. The name of the item is always displayed in the RoboDK station tree
/// </summary>
/// <returns>name of the item</returns>
QString Item::Name(){
    _RDK->_check_connection();
    _RDK->_send_Line("G_Name");
    _RDK->_send_Item(this);
    QString name = _RDK->_recv_Line();
    _RDK->_check_status();
    return name;
}

/// <summary>
/// Set the name of a RoboDK item.
/// </summary>
/// <param name="name"></param>
void Item::setName(const QString &name){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Name");
    _RDK->_send_Item(this);
    _RDK->_send_Line(name);
    _RDK->_check_status();
}

// add more methods

/// <summary>
/// Sets the local position (pose) of an object, target or reference frame. For example, the position of an object/frame/target with respect to its parent.
/// If a robot is provided, it will set the pose of the end efector.
/// </summary>
/// <param name="pose">4x4 homogeneous matrix</param>
void Item::setPose(Mat pose){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Hlocal");
    _RDK->_send_Item(this);
    _RDK->_send_Pose(pose);
    _RDK->_check_status();
}

/// <summary>
/// Returns the local position (pose) of an object, target or reference frame. For example, the position of an object/frame/target with respect to its parent.
/// If a robot is provided, it will get the pose of the end efector
/// </summary>
/// <returns>4x4 homogeneous matrix (pose)</returns>
Mat Item::Pose(){
    _RDK->_check_connection();
    _RDK->_send_Line("G_Hlocal");
    _RDK->_send_Item(this);
    Mat pose = _RDK->_recv_Pose();
    _RDK->_check_status();
    return pose;
}

/// <summary>
/// Sets the position (pose) the object geometry with respect to its own reference frame. This procedure works for tools and objects.
/// </summary>
/// <param name="pose">4x4 homogeneous matrix</param>
void Item::setGeometryPose(Mat pose){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Hgeom");
    _RDK->_send_Item(this);
    _RDK->_send_Pose(pose);
    _RDK->_check_status();
}

/// <summary>
/// Returns the position (pose) the object geometry with respect to its own reference frame. This procedure works for tools and objects.
/// </summary>
/// <returns>4x4 homogeneous matrix (pose)</returns>
Mat Item::GeometryPose(){
    _RDK->_check_connection();
    _RDK->_send_Line("G_Hgeom");
    _RDK->_send_Item(this);
    Mat pose = _RDK->_recv_Pose();
    _RDK->_check_status();
    return pose;
}

/// <summary>
/// Obsolete: Use setPoseTool(pose) instead. Sets the tool pose of a tool item. If a robot is provided it will set the tool pose of the active tool held by the robot.
/// </summary>
/// <param name="pose">4x4 homogeneous matrix (pose)</param>
void Item::setHtool(Mat pose){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Htool");
    _RDK->_send_Item(this);
    _RDK->_send_Pose(pose);
    _RDK->_check_status();
}

/// <summary>
/// Obsolete: Use PoseTool() instead.
/// Returns the tool pose of an item. If a robot is provided it will get the tool pose of the active tool held by the robot.
/// </summary>
/// <returns>4x4 homogeneous matrix (pose)</returns>
Mat Item::Htool(){
    _RDK->_check_connection();
    _RDK->_send_Line("G_Htool");
    _RDK->_send_Item(this);
    Mat pose = _RDK->_recv_Pose();
    _RDK->_check_status();
    return pose;
}

/// <summary>
/// Returns the tool pose of an item. If a robot is provided it will get the tool pose of the active tool held by the robot.
/// </summary>
/// <returns>4x4 homogeneous matrix (pose)</returns>
Mat Item::PoseTool(){
    _RDK->_check_connection();
    _RDK->_send_Line("G_Tool");
    _RDK->_send_Item(this);
    Mat pose = _RDK->_recv_Pose();
    _RDK->_check_status();
    return pose;
}

/// <summary>
/// Returns the reference frame pose of an item. If a robot is provided it will get the tool pose of the active reference frame used by the robot.
/// </summary>
/// <returns>4x4 homogeneous matrix (pose)</returns>
Mat Item::PoseFrame(){
    _RDK->_check_connection();
    _RDK->_send_Line("G_Frame");
    _RDK->_send_Item(this);
    Mat pose = _RDK->_recv_Pose();
    _RDK->_check_status();
    return pose;
}

/// <summary>
/// Sets the reference frame of a robot(user frame). The frame can be either an item or a pose.
/// If "frame" is an item, it links the robot to the frame item. If frame is a pose, it updates the linked pose of the robot frame (with respect to the robot reference frame).
/// </summary>
/// <param name="frame_pose">4x4 homogeneous matrix (pose)</param>
void Item::setPoseFrame(Mat frame_pose){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Frame");
    _RDK->_send_Pose(frame_pose);
    _RDK->_send_Item(this);
    _RDK->_check_status();
}

/// <summary>
/// Sets the tool of a robot or a tool object (Tool Center Point, or TCP). The tool pose can be either an item or a 4x4 Matrix.
/// If the item is a tool, it links the robot to the tool item.If tool is a pose, it updates the current robot TCP.
/// </summary>
/// <param name="pose">4x4 homogeneous matrix (pose)</param>
void Item::setPoseFrame(Item frame_item){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Frame_ptr");
    _RDK->_send_Item(frame_item);
    _RDK->_send_Item(this);
    _RDK->_check_status();
}

/// <summary>
/// Sets the tool of a robot or a tool object (Tool Center Point, or TCP). The tool pose can be either an item or a 4x4 Matrix.
/// If the item is a tool, it links the robot to the tool item.If tool is a pose, it updates the current robot TCP.
/// </summary>
/// <param name="tool_pose">4x4 homogeneous matrix (pose)</param>
void Item::setPoseTool(Mat tool_pose){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Tool");
    _RDK->_send_Pose(tool_pose);
    _RDK->_send_Item(this);
    _RDK->_check_status();
}

/// <summary>
/// Sets the tool of a robot or a tool object (Tool Center Point, or TCP). The tool pose can be either an item or a 4x4 Matrix.
/// If the item is a tool, it links the robot to the tool item.If tool is a pose, it updates the current robot TCP.
/// </summary>
/// <param name="tool_item">Tool item</param>
void Item::setPoseTool(Item tool_item){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Tool_ptr");
    _RDK->_send_Item(tool_item);
    _RDK->_send_Item(this);
    _RDK->_check_status();
}

/// <summary>
/// Sets the global position (pose) of an item. For example, the position of an object/frame/target with respect to the station origin.
/// </summary>
/// <param name="pose">4x4 homogeneous matrix (pose)</param>
void Item::setPoseAbs(Mat pose){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Hlocal_Abs");
    _RDK->_send_Item(this);
    _RDK->_send_Pose(pose);
    _RDK->_check_status();

}

/// <summary>
/// Returns the global position (pose) of an item. For example, the position of an object/frame/target with respect to the station origin.
/// </summary>
/// <returns>4x4 homogeneous matrix (pose)</returns>
Mat Item::PoseAbs(){
    _RDK->_check_connection();
    _RDK->_send_Line("G_Hlocal_Abs");
    _RDK->_send_Item(this);
    Mat pose = _RDK->_recv_Pose();
    _RDK->_check_status();
    return pose;
}

/// <summary>
/// Set the color of an object, tool or robot.
/// A color must in the format COLOR=[R,G,B,(A=1)] where all values range from 0 to 1.
/// <summary>
void Item::setColor(double colorRGBA[4]){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Color");
    _RDK->_send_Item(this);
    _RDK->_send_Array(colorRGBA, 4);
    _RDK->_check_status();

}

///--------------------------------- add curve, scale, recolor, ...
///
///
///


/// <summary>
/// Apply a scale to an object to make it bigger or smaller.
/// The scale can be uniform (if scale is a float value) or per axis (if scale is a vector).
/// </summary>
/// <param name="scale">scale to apply as scale or [scale_x, scale_y, scale_z]</param>
void Item::Scale(double scale){
    double scale_xyz[3];
    scale_xyz[0] = scale;
    scale_xyz[1] = scale;
    scale_xyz[2] = scale;
    Scale(scale_xyz);
}

/// <summary>
/// Apply a per-axis scale to an object to make it bigger or smaller.
/// The scale can be uniform (if scale is a float value) or per axis (if scale is a vector).
/// </summary>
/// <param name="scale">scale to apply as [scale_x, scale_y, scale_z]</param>
void Item::Scale(double scale_xyz[3]){
    _RDK->_check_connection();
    _RDK->_send_Line("Scale");
    _RDK->_send_Item(this);
    _RDK->_send_Array(scale_xyz, 3);
    _RDK->_check_status();
}




/// <summary>
/// Update the robot milling path input and parameters. Parameter input can be an NC file (G-code or APT file) or an object item in RoboDK. A curve or a point follow project will be automatically set up for a robot manufacturing project.
/// Tip: Use getLink() and setLink() to get/set the robot tool, reference frame, robot and program linked to the project.
/// Tip: Use setPose() and setJoints() to update the path to tool orientation or the preferred start joints.
/// </summary>
/// <param name="ncfile">path to the NC (G-code/APT/Point cloud) file to load (optional)</param>
/// <param name="part_obj">object holding curves or points to automatically set up a curve/point follow project (optional)</param>
/// <param name="options">Additional options (optional)</param>
/// <returns>Program linked to the project (invalid item if failed to update). Use Update() to retrieve the result</returns>
Item Item::setMachiningParameters(QString ncfile, Item part_obj, QString options)
{
    _RDK->_check_connection();
    _RDK->_send_Line("S_MachiningParams");
    _RDK->_send_Item(this);
    _RDK->_send_Line(ncfile);
    _RDK->_send_Item(part_obj);
    _RDK->_send_Line("NO_UPDATE " + options);
    _RDK->_TIMEOUT = 3600 * 1000;
    Item program = _RDK->_recv_Item();
    _RDK->_TIMEOUT = ROBODK_API_TIMEOUT;
    double status = _RDK->_recv_Int() / 1000.0;
    _RDK->_check_status();
    return program;
}

/// <summary>
/// Sets a target as a cartesian target. A cartesian target moves to cartesian coordinates.
/// </summary>
void Item::setAsCartesianTarget(){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Target_As_RT");
    _RDK->_send_Item(this);
    _RDK->_check_status();
}

/// <summary>
/// Sets a target as a joint target. A joint target moves to a joints position without regarding the cartesian coordinates.
/// </summary>
void Item::setAsJointTarget(){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Target_As_JT");
    _RDK->_send_Item(this);
    _RDK->_check_status();
}

/// <summary>
/// Returns True if a target is a joint target (green icon). Otherwise, the target is a Cartesian target (red icon).
/// </summary>
bool Item::isJointTarget(){
    _RDK->_check_connection();
    _RDK->_send_Line("Target_Is_JT");
    _RDK->_send_Item(this);
    int is_jt = _RDK->_recv_Int();
    _RDK->_check_status();
    return is_jt > 0;
}

//#####Robot item calls####

/// <summary>
/// Returns the current joints of a robot or the joints of a target. If the item is a cartesian target, it returns the preferred joints (configuration) to go to that cartesian position.
/// </summary>
/// <returns>double x n -> joints matrix</returns>
tJoints Item::Joints(){
    tJoints jnts;
    _RDK->_check_connection();
    _RDK->_send_Line("G_Thetas");
    _RDK->_send_Item(this);
    _RDK->_recv_Array(&jnts);
    _RDK->_check_status();
    return jnts;
}

// add more methods

/// <summary>
/// Returns the home joints of a robot. These joints can be manually set in the robot "Parameters" menu, then select "Set home position"
/// </summary>
/// <returns>double x n -> joints array</returns>
tJoints Item::JointsHome(){
    tJoints jnts;
    _RDK->_check_connection();
    _RDK->_send_Line("G_Home");
    _RDK->_send_Item(this);
    _RDK->_recv_Array(&jnts);
    _RDK->_check_status();
    return jnts;
}


/// <summary>
/// Sets the joint position of the "home" joints for a robot.
/// </summary>
/// <param name="joints"></param>
void Item::setJointsHome(const tJoints &jnts){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Home");
    _RDK->_send_Array(&jnts);
    _RDK->_send_Item(this);
    _RDK->_check_status();
}

/// <summary>
/// Returns an item pointer (:class:`.Item`) to a robot link. This is useful to show/hide certain robot links or alter their geometry.
/// </summary>
/// <param name="link_id">link index(0 for the robot base, 1 for the first link, ...)</param>
/// <returns></returns>
Item Item::ObjectLink(int link_id){
    _RDK->_check_connection();
    _RDK->_send_Line("G_LinkObjId");
    _RDK->_send_Item(this);
    _RDK->_send_Int(link_id);
    Item item = _RDK->_recv_Item();
    _RDK->_check_status();
    return item;
}

/// <summary>
/// Returns an item pointer (Item class) to a robot, object, tool or program. This is useful to retrieve the relationship between programs, robots, tools and other specific projects.
/// </summary>
/// <param name="type_linked">type of linked object to retrieve</param>
/// <returns></returns>
Item Item::getLink(int type_linked){
    _RDK->_check_connection();
    _RDK->_send_Line("G_LinkType");
    _RDK->_send_Item(this);
    _RDK->_send_Int(type_linked);
    Item item = _RDK->_recv_Item();
    _RDK->_check_status();
    return item;
}


/// <summary>
/// Sets the current joints of a robot or the joints of a target. It the item is a cartesian target, it returns the preferred joints (configuration) to go to that cartesian position.
/// </summary>
/// <param name="joints"></param>
void Item::setJoints(const tJoints &jnts){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Thetas");
    _RDK->_send_Array(&jnts);
    _RDK->_send_Item(this);
    _RDK->_check_status();
}

/// <summary>
/// Returns the joint limits of a robot
/// </summary>
/// <param name="lower_limits"></param>
/// <param name="upper_limits"></param>
void Item::JointLimits(tJoints *lower_limits, tJoints *upper_limits){
    _RDK->_check_connection();
    _RDK->_send_Line("G_RobLimits");
    _RDK->_send_Item(this);
    _RDK->_recv_Array(lower_limits);
    _RDK->_recv_Array(upper_limits);
    double joints_type = _RDK->_recv_Int() / 1000.0;
    _RDK->_check_status();
}

/// <summary>
/// Sets the robot of a program or a target. You must set the robot linked to a program or a target every time you copy paste these objects.
/// If the robot is not provided, the first available robot will be chosen automatically.
/// </summary>
/// <param name="robot">Robot item</param>
void Item::setRobot(const Item &robot){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Robot");
    _RDK->_send_Item(this);
    _RDK->_send_Item(robot);
    _RDK->_check_status();
}


/// <summary>
/// Adds an empty tool to the robot provided the tool pose (4x4 Matrix) and the tool name.
/// </summary>
/// <param name="tool_pose">pose -> TCP as a 4x4 Matrix (pose of the tool frame)</param>
/// <param name="tool_name">New tool name</param>
/// <returns>new item created</returns>
Item Item::AddTool(const Mat &tool_pose, const QString &tool_name){
    _RDK->_check_connection();
    _RDK->_send_Line("AddToolEmpty");
    _RDK->_send_Item(this);
    _RDK->_send_Pose(tool_pose);
    _RDK->_send_Line(tool_name);
    Item newtool = _RDK->_recv_Item();
    _RDK->_check_status();
    return newtool;
}

/// <summary>
/// Computes the forward kinematics of the robot for the provided joints. The tool and the reference frame are not taken into account.
/// </summary>
/// <param name="joints"></param>
/// <returns>4x4 homogeneous matrix: pose of the robot flange with respect to the robot base</returns>
Mat Item::SolveFK(const tJoints &joints){
    _RDK->_check_connection();
    _RDK->_send_Line("G_FK");
    _RDK->_send_Array(&joints);
    _RDK->_send_Item(this);
    Mat pose = _RDK->_recv_Pose();
    _RDK->_check_status();
    return pose;
}

/// <summary>
/// Returns the robot configuration state for a set of robot joints.
/// </summary>
/// <param name="joints">array of joints</param>
/// <returns>3-array -> configuration status as [REAR, LOWERARM, FLIP]</returns>
void Item::JointsConfig(const tJoints &joints, tConfig config){
    _RDK->_check_connection();
    _RDK->_send_Line("G_Thetas_Config");
    _RDK->_send_Array(&joints);
    _RDK->_send_Item(this);
    int sz = RDK_SIZE_MAX_CONFIG;
    _RDK->_recv_Array(config, &sz);
    _RDK->_check_status();
    //return config;
}

/// <summary>
/// Computes the inverse kinematics for the specified robot and pose. The joints returned are the closest to the current robot configuration (see SolveIK_All())
/// </summary>
/// <param name="pose">4x4 matrix -> pose of the robot flange with respect to the robot base frame</param>
/// <returns>array of joints</returns>
tJoints Item::SolveIK(const Mat &pose, const Mat *tool, const Mat *ref){
    tJoints jnts;
    Mat base2flange(pose);
    if (tool != nullptr){
        base2flange = pose*tool->inv();
    }
    if (ref != nullptr){
        base2flange = (*ref) * base2flange;
    }
    _RDK->_check_connection();
    _RDK->_send_Line("G_IK");
    _RDK->_send_Pose(base2flange);
    _RDK->_send_Item(this);
    _RDK->_recv_Array(&jnts);
    _RDK->_check_status();
    return jnts;
}

/// <summary>
/// Computes the inverse kinematics for the specified robot and pose. The function returns all available joint solutions as a 2D matrix.
/// </summary>
/// <param name="pose">4x4 matrix -> pose of the robot tool with respect to the robot frame</param>
/// <returns>double x n x m -> joint list (2D matrix)</returns>
tMatrix2D* Item::SolveIK_All_Mat2D(const Mat &pose, const Mat *tool, const Mat *ref){
    tMatrix2D *mat2d = nullptr;
    Mat base2flange(pose);
    if (tool != nullptr){
        base2flange = pose*tool->inv();
    }
    if (ref != nullptr){
        base2flange = (*ref) * base2flange;
    }
    _RDK->_check_connection();
    _RDK->_send_Line("G_IK_cmpl");
    _RDK->_send_Pose(base2flange);
    _RDK->_send_Item(this);
    _RDK->_recv_Matrix2D(&mat2d);
    _RDK->_check_status();
    return mat2d;
}
QList<tJoints> Item::SolveIK_All(const Mat &pose, const Mat *tool, const Mat *ref){
    tMatrix2D *mat2d = SolveIK_All_Mat2D(pose, tool, ref);
    QList<tJoints> jnts_list;
    int ndofs = Matrix2D_Size(mat2d, 1) - 2;
    for (int i=0; i<Matrix2D_Size(mat2d, 2); i++){
        jnts_list.append(tJoints(mat2d, i));
    }
    return jnts_list;
}

/// <summary>
/// Connect to a real robot using the robot driver.
/// </summary>
/// <param name="robot_ip">IP of the robot to connect. Leave empty to use the one defined in RoboDK</param>
/// <returns>status -> true if connected successfully, false if connection failed</returns>
bool Item::Connect(const QString &robot_ip){
    _RDK->_check_connection();
    _RDK->_send_Line("Connect");
    _RDK->_send_Item(this);
    _RDK->_send_Line(robot_ip);
    int status = _RDK->_recv_Int();
    _RDK->_check_status();
    return status != 0;
}

/// <summary>
/// Disconnect from a real robot (when the robot driver is used)
/// </summary>
/// <returns>status -> true if disconnected successfully, false if it failed. It can fail if it was previously disconnected manually for example.</returns>
bool Item::Disconnect(){
    _RDK->_check_connection();
    _RDK->_send_Line("Disconnect");
    _RDK->_send_Item(this);
    int status = _RDK->_recv_Int();
    _RDK->_check_status();
    return status != 0;
}

/// <summary>
/// Moves a robot to a specific target ("Move Joint" mode). By default, this function blocks until the robot finishes its movements.
/// </summary>
/// <param name="target">target -> target to move to as a target item (RoboDK target item)</param>
/// <param name="blocking">blocking -> True if we want the instruction to block until the robot finished the movement (default=true)</param>
void Item::MoveJ(const Item &itemtarget, bool blocking){
    if (_TYPE == RoboDK::ITEM_TYPE_PROGRAM){
        _RDK->_check_connection();
        _RDK->_send_Line("Add_INSMOVE");
        _RDK->_send_Item(itemtarget);
        _RDK->_send_Item(this);
        _RDK->_send_Int(1);
        _RDK->_check_status();
    } else {
        _RDK->_moveX(&itemtarget, NULL, NULL, this, 1, blocking);
    }
}

/// <summary>
/// Moves a robot to a specific target ("Move Joint" mode). By default, this function blocks until the robot finishes its movements.
/// </summary>
/// <param name="target">joints -> joint target to move to.</param>
/// <param name="blocking">blocking -> True if we want the instruction to block until the robot finished the movement (default=true)</param>
void Item::MoveJ(const tJoints &joints, bool blocking){
    _RDK->_moveX(NULL, &joints, NULL, this, 1, blocking);
}

/// <summary>
/// Moves a robot to a specific target ("Move Joint" mode). By default, this function blocks until the robot finishes its movements.
/// </summary>
/// <param name="target">pose -> pose target to move to. It must be a 4x4 Homogeneous matrix</param>
/// <param name="blocking">blocking -> True if we want the instruction to block until the robot finished the movement (default=true)</param>
void Item::MoveJ(const Mat &target, bool blocking){
    _RDK->_moveX(NULL, NULL, &target, this, 1, blocking);
}

/// <summary>
/// Moves a robot to a specific target ("Move Linear" mode). By default, this function blocks until the robot finishes its movements.
/// </summary>
/// <param name="itemtarget">target -> target to move to as a target item (RoboDK target item)</param>
/// <param name="blocking">blocking -> True if we want the instruction to block until the robot finished the movement (default=true)</param>
void Item::MoveL(const Item &itemtarget, bool blocking){
    if (_TYPE == RoboDK::ITEM_TYPE_PROGRAM){
        _RDK->_check_connection();
        _RDK->_send_Line("Add_INSMOVE");
        _RDK->_send_Item(itemtarget);
        _RDK->_send_Item(this);
        _RDK->_send_Int(2);
        _RDK->_check_status();
    } else {
        _RDK->_moveX(&itemtarget, NULL, NULL, this, 2, blocking);
    }
}

/// <summary>
/// Moves a robot to a specific target ("Move Linear" mode). By default, this function blocks until the robot finishes its movements.
/// </summary>
/// <param name="joints">joints -> joint target to move to.</param>
/// <param name="blocking">blocking -> True if we want the instruction to block until the robot finished the movement (default=true)</param>
void Item::MoveL(const tJoints &joints, bool blocking){
    _RDK->_moveX(NULL, &joints, NULL, this, 2, blocking);
}

/// <summary>
/// Moves a robot to a specific target ("Move Linear" mode). By default, this function blocks until the robot finishes its movements.
/// </summary>
/// <param name="target">pose -> pose target to move to. It must be a 4x4 Homogeneous matrix</param>
/// <param name="blocking">blocking -> True if we want the instruction to block until the robot finished the movement (default=true)</param>
void Item::MoveL(const Mat &target, bool blocking){
    _RDK->_moveX(NULL, NULL, &target, this, 2, blocking);
}

/// <summary>
/// Moves a robot to a specific target ("Move Circular" mode). By default, this function blocks until the robot finishes its movements.
/// </summary>
/// <param name="itemtarget1">target -> intermediate target to move to as a target item (RoboDK target item)</param>
/// <param name="itemtarget2">target -> final target to move to as a target item (RoboDK target item)</param>
/// <param name="blocking">blocking -> True if we want the instruction to block until the robot finished the movement (default=true)</param>
void Item::MoveC(const Item &itemtarget1, const Item &itemtarget2, bool blocking){
    _RDK->_moveC(&itemtarget1, NULL, NULL, &itemtarget2, NULL, NULL, this, blocking);
}

/// <summary>
/// Moves a robot to a specific target ("Move Circular" mode). By default, this function blocks until the robot finishes its movements.
/// </summary>
/// <param name="joints1">joints -> intermediate joint target to move to.</param>
/// <param name="joints2">joints -> final joint target to move to.</param>
/// <param name="blocking">blocking -> True if we want the instruction to block until the robot finished the movement (default=true)</param>
void Item::MoveC(const tJoints &joints1, const tJoints &joints2, bool blocking){
    _RDK->_moveC(NULL, &joints1, NULL, NULL, &joints2, NULL, this, blocking);
}

/// <summary>
/// Moves a robot to a specific target ("Move Circular" mode). By default, this function blocks until the robot finishes its movements.
/// </summary>
/// <param name="target1">pose -> intermediate pose target to move to. It must be a 4x4 Homogeneous matrix</param>
/// <param name="target2">pose -> final pose target to move to. It must be a 4x4 Homogeneous matrix</param>
/// <param name="blocking">blocking -> True if we want the instruction to block until the robot finished the movement (default=true)</param>
void Item::MoveC(const Mat &target1, const Mat &target2, bool blocking){
    _RDK->_moveC(NULL, NULL, &target1, NULL, NULL, &target2, this, blocking);
}

/// <summary>
/// Checks if a joint movement is free of collision.
/// </summary>
/// <param name="j1">joints -> start joints</param>
/// <param name="j2">joints -> destination joints</param>
/// <param name="minstep_deg">(optional): maximum joint step in degrees</param>
/// <returns>collision : returns 0 if the movement is free of collision. Otherwise it returns the number of pairs of objects that collided if there was a collision.</returns>
int Item::MoveJ_Test(const tJoints &j1, const tJoints &j2, double minstep_deg){
    _RDK->_check_connection();
    _RDK->_send_Line("CollisionMove");
    _RDK->_send_Item(this);
    _RDK->_send_Array(&j1);
    _RDK->_send_Array(&j2);
    _RDK->_send_Int((int)(minstep_deg * 1000.0));
    _RDK->_TIMEOUT = 3600 * 1000;
    int collision = _RDK->_recv_Int();
    _RDK->_TIMEOUT = ROBODK_API_TIMEOUT;
    _RDK->_check_status();
    return collision;
}

/// <summary>
/// Checks if a linear movement is free of collision.
/// </summary>
/// <param name="j1">joints -> start joints</param>
/// <param name="pose2">joints -> destination pose (active tool with respect to the active reference frame)</param>
/// <param name="minstep_mm">(optional): maximum joint step in degrees</param>
/// <returns>collision : returns 0 if the movement is free of collision. Otherwise it returns the number of pairs of objects that collided if there was a collision.</returns>
int Item::MoveL_Test(const tJoints &j1, const Mat &pose2, double minstep_deg){
    _RDK->_check_connection();
    _RDK->_send_Line("CollisionMoveL");
    _RDK->_send_Item(this);
    _RDK->_send_Array(&j1);
    _RDK->_send_Pose(pose2);
    _RDK->_send_Int((int)(minstep_deg * 1000.0));
    _RDK->_TIMEOUT = 3600 * 1000;
    int collision = _RDK->_recv_Int();
    _RDK->_TIMEOUT = ROBODK_API_TIMEOUT;
    _RDK->_check_status();
    return collision;
}


/// <summary>
/// Sets the speed and/or the acceleration of a robot.
/// </summary>
/// <param name="speed_linear">linear speed in mm/s (-1 = no change)</param>
/// <param name="accel_linear">linear acceleration in mm/s2 (-1 = no change)</param>
/// <param name="speed_joints">joint speed in deg/s (-1 = no change)</param>
/// <param name="accel_joints">joint acceleration in deg/s2 (-1 = no change)</param>
void Item::setSpeed(double speed_linear, double accel_linear, double speed_joints, double accel_joints){
    _RDK->_check_connection();
    _RDK->_send_Line("S_Speed4");
    _RDK->_send_Item(this);
    double speed_accel[4];
    speed_accel[0] = speed_linear;
    speed_accel[1] = accel_linear;
    speed_accel[2] = speed_joints;
    speed_accel[3] = accel_joints;
    _RDK->_send_Array(speed_accel, 4);
    _RDK->_check_status();
}

/// <summary>
/// Sets the robot movement smoothing accuracy (also known as zone data value).
/// </summary>
/// <param name="zonedata">zonedata value (int) (robot dependent, set to -1 for fine movements)</param>
void Item::setRounding(double zonedata){
    _RDK->_check_connection();
    _RDK->_send_Line("S_ZoneData");
    _RDK->_send_Int((int)(zonedata * 1000.0));
    _RDK->_send_Item(this);
    _RDK->_check_status();
}

/// <summary>
/// Displays a sequence of joints
/// </summary>
/// <param name="sequence">joint sequence as a 6xN matrix or instruction sequence as a 7xN matrix</param>
void Item::ShowSequence(tMatrix2D *sequence){
    _RDK->_check_connection();
    _RDK->_send_Line("Show_Seq");
    _RDK->_send_Matrix2D(sequence);
    _RDK->_send_Item(this);
    _RDK->_check_status();
}


/// <summary>
/// Checks if a robot or program is currently running (busy or moving)
/// </summary>
/// <returns>busy status (true=moving, false=stopped)</returns>
bool Item::Busy(){
    _RDK->_check_connection();
    _RDK->_send_Line("IsBusy");
    _RDK->_send_Item(this);
    int busy = _RDK->_recv_Int();
    _RDK->_check_status();
    return (busy > 0);
}

/// <summary>
/// Stops a program or a robot
/// </summary>
/// <returns></returns>
void Item::Stop(){
    _RDK->_check_connection();
    _RDK->_send_Line("Stop");
    _RDK->_send_Item(this);
    _RDK->_check_status();
}

/// <summary>
/// Waits (blocks) until the robot finishes its movement.
/// </summary>
/// <param name="timeout_sec">timeout -> Max time to wait for robot to finish its movement (in seconds)</param>
void Item::WaitMove(double timeout_sec) const{
    _RDK->_check_connection();
    _RDK->_send_Line("WaitMove");
    _RDK->_send_Item(this);
    _RDK->_check_status();
    _RDK->_TIMEOUT = (int)(timeout_sec * 1000.0);
    _RDK->_check_status();//will wait here;
    _RDK->_TIMEOUT = ROBODK_API_TIMEOUT;
    //int isbusy = _RDK->Busy(this);
    //while (isbusy)
    //{
    //    busy = _RDK->Busy(item);
    //}
}

///////// ADD MORE METHODS


// ---- Program item calls -----

/// <summary>
/// Saves a program to a file.
/// </summary>
/// <param name="filename">File path of the program</param>
/// <returns>success</returns>
bool Item::MakeProgram(const QString &filename){
    _RDK->_check_connection();
    _RDK->_send_Line("MakeProg");
    _RDK->_send_Item(this);
    _RDK->_send_Line(filename);
    int prog_status = _RDK->_recv_Int();
    QString prog_log_str = _RDK->_recv_Line();
    _RDK->_check_status();
    bool success = false;
    if (prog_status > 1) {
        success = true;
    }
    return success; // prog_log_str
}

/// <summary>
/// Sets if the program will be run in simulation mode or on the real robot.
/// Use: "PROGRAM_RUN_ON_SIMULATOR" to set the program to run on the simulator only or "PROGRAM_RUN_ON_ROBOT" to force the program to run on the robot.
/// </summary>
/// <returns>number of instructions that can be executed</returns>
void Item::setRunType(int program_run_type){
    _RDK->_check_connection();
    _RDK->_send_Line("S_ProgRunType");
    _RDK->_send_Item(this);
    _RDK->_send_Int(program_run_type);
    _RDK->_check_status();
}

/// <summary>
/// Runs a program. It returns the number of instructions that can be executed successfully (a quick program check is performed before the program starts)
/// This is a non-blocking call. Use IsBusy() to check if the program execution finished.
/// Notes:
/// if setRunMode(RUNMODE_SIMULATE) is used  -> the program will be simulated (default run mode)
/// if setRunMode(RUNMODE_RUN_ROBOT) is used -> the program will run on the robot (default when RUNMODE_RUN_ROBOT is used)
/// if setRunMode(RUNMODE_RUN_ROBOT) is used together with program.setRunType(PROGRAM_RUN_ON_ROBOT) -> the program will run sequentially on the robot the same way as if we right clicked the program and selected "Run on robot" in the RoboDK GUI
/// </summary>
/// <returns>number of instructions that can be executed</returns>
int Item::RunProgram(){
    _RDK->_check_connection();
    _RDK->_send_Line("RunProg");
    _RDK->_send_Item(this);
    int prog_status = _RDK->_recv_Int();
    _RDK->_check_status();
    return prog_status;
}


/// <summary>
/// Runs a program. It returns the number of instructions that can be executed successfully (a quick program check is performed before the program starts)
/// Program parameters can be provided for Python calls.
/// This is a non-blocking call.Use IsBusy() to check if the program execution finished.
/// Notes: if setRunMode(RUNMODE_SIMULATE) is used  -> the program will be simulated (default run mode)
/// if setRunMode(RUNMODE_RUN_ROBOT) is used ->the program will run on the robot(default when RUNMODE_RUN_ROBOT is used)
/// if setRunMode(RUNMODE_RUN_ROBOT) is used together with program.setRunType(PROGRAM_RUN_ON_ROBOT) -> the program will run sequentially on the robot the same way as if we right clicked the program and selected "Run on robot" in the RoboDK GUI
/// </summary>
/// <param name="parameters">Number of instructions that can be executed</param>
int Item::RunCode(const QString &parameters){
    _RDK->_check_connection();
    if (parameters.isEmpty()){
        _RDK->_send_Line("RunProg");
        _RDK->_send_Item(this);
    } else {
        _RDK->_send_Line("RunProgParam");
        _RDK->_send_Item(this);
        _RDK->_send_Line(parameters);
    }
    int progstatus = _RDK->_recv_Int();
    _RDK->_check_status();
    return progstatus;
}

/// <summary>
/// Adds a program call, code, message or comment inside a program.
/// </summary>
/// <param name="code"><string of the code or program to run/param>
/// <param name="run_type">INSTRUCTION_* variable to specify if the code is a progra</param>
int Item::RunInstruction(const QString &code, int run_type){
    _RDK->_check_connection();
    _RDK->_send_Line("RunCode2");
    _RDK->_send_Item(this);
    _RDK->_send_Line(QString(code).replace("\n\n", "<br>").replace("\n", "<br>"));
    _RDK->_send_Int(run_type);
    int progstatus = _RDK->_recv_Int();
    _RDK->_check_status();
    return progstatus;
}

/// <summary>
/// Generates a pause instruction for a robot or a program when generating code. Set it to -1 (default) if you want the robot to stop and let the user resume the program anytime.
/// </summary>
/// <param name="time_ms">Time in milliseconds</param>
void Item::Pause(double time_ms){
    _RDK->_check_connection();
    _RDK->_send_Line("RunPause");
    _RDK->_send_Item(this);
    _RDK->_send_Int((int)(time_ms * 1000.0));
    _RDK->_check_status();
}


/// <summary>
/// Sets a variable (output) to a given value. This can also be used to set any variables to a desired value.
/// </summary>
/// <param name="io_var">io_var -> digital output (string or number)</param>
/// <param name="io_value">io_value -> value (string or number)</param>
void Item::setDO(const QString &io_var, const QString &io_value){
    _RDK->_check_connection();
    _RDK->_send_Line("setDO");
    _RDK->_send_Item(this);
    _RDK->_send_Line(io_var);
    _RDK->_send_Line(io_value);
    _RDK->_check_status();
}

/// <summary>
/// Waits for an input io_id to attain a given value io_value. Optionally, a timeout can be provided.
/// </summary>
/// <param name="io_var">io_var -> digital output (string or number)</param>
/// <param name="io_value">io_value -> value (string or number)</param>
/// <param name="timeout_ms">int (optional) -> timeout in miliseconds</param>
void Item::waitDI(const QString &io_var, const QString &io_value, double timeout_ms){
    _RDK->_check_connection();
    _RDK->_send_Line("waitDI");
    _RDK->_send_Item(this);
    _RDK->_send_Line(io_var);
    _RDK->_send_Line(io_value);
    _RDK->_send_Int((int)(timeout_ms * 1000.0));
    _RDK->_check_status();
}

/// <summary>
/// Add a custom instruction. This instruction will execute a Python file or an executable file.
/// </summary>
/// <param name="name"></param>
/// <param name="path_run">path to run(relative to RoboDK/bin folder or absolute path)</param>
/// <param name="path_icon">icon path(relative to RoboDK/bin folder or absolute path)</param>
/// <param name="blocking">True if blocking, 0 if it is a non blocking executable trigger</param>
/// <param name="cmd_run_on_robot">Command to run through the driver when connected to the robot</param>
/// :param name: digital input (string or number)
void Item::customInstruction(const QString &name, const QString &path_run, const QString &path_icon, bool blocking, const QString &cmd_run_on_robot){
    _RDK->_check_connection();
    _RDK->_send_Line("InsCustom2");
    _RDK->_send_Item(this);
    _RDK->_send_Line(name);
    _RDK->_send_Line(path_run);
    _RDK->_send_Line(path_icon);
    _RDK->_send_Line(cmd_run_on_robot);
    _RDK->_send_Int(blocking ? 1 : 0);
    _RDK->_check_status();
}

/*
/////// obsolete functions
/// <summary>
/// Adds a new robot move joint instruction to a program.
/// </summary>
/// <param name="itemtarget">target to move to</param>
void Item::addMoveJ(const Item &itemtarget){
    _RDK->_check_connection();
    _RDK->_send_Line("Add_INSMOVE");
    _RDK->_send_Item(itemtarget);
    _RDK->_send_Item(this);
    _RDK->_send_Int(1);
    _RDK->_check_status();
}

/// <summary>
/// Adds a new robot move linear instruction to a program.
/// </summary>
/// <param name="itemtarget">target to move to</param>
void Item::addMoveL(const Item &itemtarget){
    _RDK->_check_connection();
    _RDK->_send_Line("Add_INSMOVE");
    _RDK->_send_Item(itemtarget);
    _RDK->_send_Item(this);
    _RDK->_send_Int(2);
    _RDK->_check_status();
}
*/

/// <summary>
/// Show or hide instruction items of a program in the RoboDK tree
/// </summary>
/// <param name="show"></param>
void Item::ShowInstructions(bool visible){
    _RDK->_check_connection();
    _RDK->_send_Line("Prog_ShowIns");
    _RDK->_send_Item(this);
    _RDK->_send_Int(visible ? 1 : 0);
    _RDK->_check_status();
}

/// <summary>
/// Show or hide targets of a program in the RoboDK tree
/// </summary>
/// <param name="show"></param>
void Item::ShowTargets(bool visible){
    _RDK->_check_connection();
    _RDK->_send_Line("Prog_ShowTargets");
    _RDK->_send_Item(this);
    _RDK->_send_Int(visible ? 1 : 0);
    _RDK->_check_status();
}


////////// ADD MORE METHODS
/// <summary>
/// Returns the number of instructions of a program.
/// </summary>
/// <returns></returns>
int Item::InstructionCount(){
    _RDK->_check_connection();
    _RDK->_send_Line("Prog_Nins");
    _RDK->_send_Item(this);
    int nins = _RDK->_recv_Int();
    _RDK->_check_status();
    return nins;
}

/// <summary>
/// Returns the program instruction at position id
/// </summary>
/// <param name="ins_id"></param>
/// <param name="name"></param>
/// <param name="instype"></param>
/// <param name="movetype"></param>
/// <param name="isjointtarget"></param>
/// <param name="target"></param>
/// <param name="joints"></param>
void Item::Instruction(int ins_id, QString &name, int &instype, int &movetype, bool &isjointtarget, Mat &target, tJoints &joints){
    _RDK->_check_connection();
    _RDK->_send_Line("Prog_GIns");
    _RDK->_send_Item(this);
    _RDK->_send_Int(ins_id);
    name = _RDK->_recv_Line();
    instype = _RDK->_recv_Int();
    movetype = 0;
    isjointtarget = false;
    //target = null;
    //joints = null;
    if (instype == RoboDK::INS_TYPE_MOVE) {
        movetype = _RDK->_recv_Int();
        isjointtarget = _RDK->_recv_Int() > 0 ? true : false;
        target = _RDK->_recv_Pose();
        _RDK->_recv_Array(&joints);
    }
    _RDK->_check_status();
}

/// <summary>
/// Sets the program instruction at position id
/// </summary>
/// <param name="ins_id"></param>
/// <param name="name"></param>
/// <param name="instype"></param>
/// <param name="movetype"></param>
/// <param name="isjointtarget"></param>
/// <param name="target"></param>
/// <param name="joints"></param>
void Item::setInstruction(int ins_id, const QString &name, int instype, int movetype, bool isjointtarget, const Mat &target, const tJoints &joints){
    _RDK->_check_connection();
    _RDK->_send_Line("Prog_SIns");
    _RDK->_send_Item(this);
    _RDK->_send_Int(ins_id);
    _RDK->_send_Line(name);
    _RDK->_send_Int(instype);
    if (instype == RoboDK::INS_TYPE_MOVE)
    {
        _RDK->_send_Int(movetype);
        _RDK->_send_Int(isjointtarget ? 1 : 0);
        _RDK->_send_Pose(target);
        _RDK->_send_Array(&joints);
    }
    _RDK->_check_status();
}


/// <summary>
/// Returns the list of program instructions as an MxN matrix, where N is the number of instructions and M equals to 1 plus the number of robot axes.
/// </summary>
/// <param name="instructions">the matrix of instructions</param>
/// <returns>Returns 0 if success</returns>
int Item::InstructionList(tMatrix2D *instructions){
    _RDK->_check_connection();
    _RDK->_send_Line("G_ProgInsList");
    _RDK->_send_Item(this);
    _RDK->_recv_Matrix2D(&instructions);
    int errors = _RDK->_recv_Int();
    _RDK->_check_status();
    return errors;
}



/// <summary>
/// Updates a program and returns the estimated time and the number of valid instructions.
/// An update can also be applied to a robot machining project. The update is performed on the generated program.
/// </summary>
/// <param name="collision_check">check_collisions: Check collisions (COLLISION_ON -yes- or COLLISION_OFF -no-)</param>
/// <param name="timeout_sec">Maximum time to wait for the update to complete (in seconds)</param>
/// <param name="out_nins_time_dist">optional double array [3] = [valid_instructions, program_time, program_distance]</param>
/// <param name="mm_step">Maximum step in millimeters for linear movements (millimeters). Set to -1 to use the default, as specified in Tools-Options-Motion.</param>
/// <param name="deg_step">Maximum step for joint movements (degrees). Set to -1 to use the default, as specified in Tools-Options-Motion.</param>
/// <returns>1.0 if there are no problems with the path or less than 1.0 if there is a problem in the path (ratio of problem)</returns>
double Item::Update(int collision_check, int timeout_sec, double *out_nins_time_dist, double mm_step, double deg_step){
    _RDK->_check_connection();
    _RDK->_send_Line("Update2");
    _RDK->_send_Item(this);
    double values[5];
    values[0] = collision_check;
    values[1] = mm_step;
    values[2] = deg_step;
    _RDK->_send_Array(values, 3);
    _RDK->_TIMEOUT = timeout_sec * 1000;
    double return_values[10];
    int nvalues = 10;
    _RDK->_recv_Array(return_values, &nvalues);
    _RDK->_TIMEOUT = ROBODK_API_TIMEOUT;
    QString readable_msg = _RDK->_recv_Line();
    _RDK->_check_status();
    double ratio_ok = return_values[3];
    if (out_nins_time_dist != NULL)
    {
        out_nins_time_dist[0] = return_values[0]; // number of correct instructions
        out_nins_time_dist[1] = return_values[1]; // estimated time to complete the program (cycle time)
        out_nins_time_dist[2] = return_values[2]; // estimated travel distance
    }
    return ratio_ok;
}


/// <summary>
/// Returns a list of joints an MxN matrix, where M is the number of robot axes plus 4 columns. Linear moves are rounded according to the smoothing parameter set inside the program.
/// </summary>
/// <param name="error_msg">Returns a human readable error message (if any)</param>
/// <param name="joint_list">Returns the list of joints as [J1, J2, ..., Jn, ERROR, MM_STEP, DEG_STEP, MOVE_ID] if a file name is not specified</param>
/// <param name="mm_step">Maximum step in millimeters for linear movements (millimeters)</param>
/// <param name="deg_step">Maximum step for joint movements (degrees)</param>
/// <param name="save_to_file">Provide a file name to directly save the output to a file. If the file name is not provided it will return the matrix. If step values are very small, the returned matrix can be very large.</param>
/// <returns>Returns 0 if success, otherwise, it will return negative values</returns>
int Item::InstructionListJoints(QString &error_msg, tMatrix2D **joint_list, double mm_step, double deg_step, const QString &save_to_file){
    _RDK->_check_connection();
    _RDK->_send_Line("G_ProgJointList");
    _RDK->_send_Item(this);
    double step_mm_deg[2] = { mm_step, deg_step };
    _RDK->_send_Array(step_mm_deg, 2);
    //joint_list = save_to_file;
    if (save_to_file.isEmpty()) {
        _RDK->_send_Line("");
        _RDK->_recv_Matrix2D(joint_list);
    } else {
        _RDK->_send_Line(save_to_file);
        joint_list = NULL;
    }
    int error_code = _RDK->_recv_Int();
    error_msg = _RDK->_recv_Line();
    _RDK->_check_status();
    return error_code;
}

/// <summary>
/// Disconnect from the RoboDK API. This flushes any pending program generation.
/// </summary>
/// <returns></returns>
bool Item::Finish(){
    _RDK->Finish();
    return true;
}

quint64 Item::GetID(){
    return _PTR;
}

//----------------------------------------  add more












//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
/////////////////////////////////// RoboDK CLASS ////////////////////////////////////////////////////
RoboDK::RoboDK(const QString &robodk_ip, int com_port, const QString &args, const QString &path) {
    _COM = NULL;
    _IP = robodk_ip;
    _TIMEOUT = ROBODK_API_TIMEOUT;
    _PROCESS = 0;
    _PORT = com_port;
    _ROBODK_BIN = path;
    if (_PORT < 0){
        _PORT = ROBODK_DEFAULT_PORT;
    }
    if (_ROBODK_BIN.isEmpty()){
        _ROBODK_BIN = ROBODK_DEFAULT_PATH_BIN;
    }
    _ARGUMENTS = args;
    if (com_port > 0){
        _ARGUMENTS.append(" /PORT=" + QString::number(com_port));
    }
    _connect_smart();
}

RoboDK::~RoboDK(){
    _disconnect();
}

quint64 RoboDK::ProcessID(){
    if (_PROCESS == 0) {
        QString response = Command("MainProcess_ID");
        _PROCESS = response.toInt();
    }
    return _PROCESS;
}

quint64 RoboDK::WindowID(){
    qint64 window_id;
    if (window_id == 0) {
        QString response = Command("MainWindow_ID");
        window_id = response.toInt();
    }
    return window_id;
}

bool RoboDK::Connected(){
    return _connected();
}

bool RoboDK::Connect(){
    return _connect();
}
/// <summary>
/// Disconnect from the RoboDK API. This flushes any pending program generation.
/// </summary>
/// <returns></returns>
void RoboDK::Disconnect(){
    _disconnect();
}
/// <summary>
/// Disconnect from the RoboDK API. This flushes any pending program generation.
/// </summary>
/// <returns></returns>
void RoboDK::Finish(){
    Disconnect();
}

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// public methods
/// <summary>
/// Returns an item by its name. If there is no exact match it will return the last closest match.
/// </summary>
/// <param name="name">Item name</param>
/// <param name="type">Filter by item type RoboDK.ITEM_TYPE_...</param>
/// <returns></returns>
Item RoboDK::getItem(QString name, int itemtype){
    _check_connection();
    if (itemtype < 0){
        _send_Line("G_Item");
        _send_Line(name);
    } else {
        _send_Line("G_Item2");
        _send_Line(name);
        _send_Int(itemtype);
    }
    Item item = _recv_Item();
    _check_status();
    return item;
}

/// <summary>
/// Returns a list of items (list of name or pointers) of all available items in the currently open station in robodk.
/// Optionally, use a filter to return specific items (example: getItemListNames(filter = ITEM_CASE_ROBOT))
/// </summary>
/// <param name="filter">ITEM_TYPE</param>
/// <returns></returns>
QStringList RoboDK::getItemListNames(int filter){
    _check_connection();
    if (filter < 0) {
        _send_Line("G_List_Items");
    } else {
        _send_Line("G_List_Items_Type");
        _send_Int(filter);
    }
    qint32 numitems = _recv_Int();
    QStringList listnames;
    for (int i = 0; i < numitems; i++) {
        listnames.append(_recv_Line());
    }
    _check_status();
    return listnames;
}

/// <summary>
/// Returns a list of items (list of name or pointers) of all available items in the currently open station in robodk.
/// Optionally, use a filter to return specific items (example: getItemListNames(filter = ITEM_CASE_ROBOT))
/// </summary>
/// <param name="filter">ITEM_TYPE</param>
/// <returns></returns>
QList<Item> RoboDK::getItemList(int filter) {
    _check_connection();
    if (filter < 0) {
        _send_Line("G_List_Items_ptr");
    } else {
        _send_Line("G_List_Items_Type_ptr");
        _send_Int(filter);
    }
    int numitems = _recv_Int();
    QList<Item> listitems;
    for (int i = 0; i < numitems; i++) {
        listitems.append(_recv_Item());
    }
    _check_status();
    return listitems;
}

/////// add more methods

/// <summary>
/// Shows a RoboDK popup to select one object from the open station.
/// An item type can be specified to filter desired items. If no type is specified, all items are selectable.
/// </summary>
/// <param name="message">Message to pop up</param>
/// <param name="itemtype">optionally filter by RoboDK.ITEM_TYPE_*</param>
/// <returns></returns>
Item RoboDK::ItemUserPick(const QString &message, int itemtype) {
    _check_connection();
    _send_Line("PickItem");
    _send_Line(message);
    _send_Int(itemtype);
    _TIMEOUT = 3600 * 1000;
    Item item = _recv_Item();//item);
    _TIMEOUT = ROBODK_API_TIMEOUT;
    _check_status();
    return item;
}

/// <summary>
/// Shows or raises the RoboDK window
/// </summary>
void RoboDK::ShowRoboDK() {
    _check_connection();
    _send_Line("RAISE");
    _check_status();
}

/// <summary>
/// Hides the RoboDK window
/// </summary>
void RoboDK::HideRoboDK() {
    _check_connection();
    _send_Line("HIDE");
    _check_status();
}

/// <summary>
/// Closes RoboDK window and finishes RoboDK execution
/// </summary>
void RoboDK::CloseRoboDK() {
    _check_connection();
    _send_Line("QUIT");
    _check_status();
    _disconnect();
    _PROCESS = 0;
}

QString RoboDK::Version()
{
    _check_connection();
    _send_Line("Version");
    QString appName = _recv_Line();
    int bitArch = _recv_Int();
    QString ver4 = _recv_Line();
    QString dateBuild = _recv_Line();
    _check_status();
    return ver4;

}


/// <summary>
/// Set the state of the RoboDK window
/// </summary>
/// <param name="windowstate"></param>
void RoboDK::setWindowState(int windowstate){
    _check_connection();
    _send_Line("S_WindowState");
    _send_Int(windowstate);
    _check_status();
}


/// <summary>
/// Update the RoboDK flags. RoboDK flags allow defining how much access the user has to RoboDK features. Use FLAG_ROBODK_* variables to set one or more flags.
/// </summary>
/// <param name="flags">state of the window(FLAG_ROBODK_*)</param>
void RoboDK::setFlagsRoboDK(int flags){
    _check_connection();
    _send_Line("S_RoboDK_Rights");
    _send_Int(flags);
    _check_status();
}

/// <summary>
/// Update item flags. Item flags allow defining how much access the user has to item-specific features. Use FLAG_ITEM_* flags to set one or more flags.
/// </summary>
/// <param name="item"></param>
/// <param name="flags"></param>
void RoboDK::setFlagsItem(Item item, int flags){
    _check_connection();
    _send_Line("S_Item_Rights");
    _send_Item(item);
    _send_Int(flags);
    _check_status();
}

/// <summary>
/// Retrieve current item flags. Item flags allow defining how much access the user has to item-specific features. Use FLAG_ITEM_* flags to set one or more flags.
/// </summary>
/// <param name="item"></param>
/// <returns></returns>
int RoboDK::getFlagsItem(Item item){
    _check_connection();
    _send_Line("S_Item_Rights");
    _send_Item(item);
    int flags = _recv_Int();
    _check_status();
    return flags;
}

/// <summary>
/// Show a message in RoboDK (it can be blocking or non blocking in the status bar)
/// </summary>
/// <param name="message">Message to display</param>
/// <param name="popup">Set to true to make the message blocking or set to false to make it non blocking</param>
void RoboDK::ShowMessage(const QString &message, bool popup){
    _check_connection();
    if (popup)
    {
        _send_Line("ShowMessage");
        _send_Line(message);
        _TIMEOUT = 3600 * 1000;
        _check_status();
        _TIMEOUT = ROBODK_API_TIMEOUT;
    }
    else
    {
        _send_Line("ShowMessageStatus");
        _send_Line(message);
        _check_status();
    }

}

/// <summary>
/// Loads a file and attaches it to parent. It can be any file supported by robodk.
/// </summary>
/// <param name="filename">absolute path of the file</param>
/// <param name="parent">parent to attach. Leave empty for new stations or to load an object at the station root</param>
/// <returns>Newly added object. Check with item.Valid() for a successful load</returns>
Item RoboDK::AddFile(const QString &filename, const Item *parent){
    _check_connection();
    _send_Line("Add");
    _send_Line(filename);
    _send_Item(parent);
    Item newitem = _recv_Item();
    _check_status();
    return newitem;
}

/// <summary>
/// Save an item to a file. If no item is provided, the open station is saved.
/// </summary>
/// <param name="filename">absolute path to save the file</param>
/// <param name="itemsave">object or station to save. Leave empty to automatically save the current station.</param>
void RoboDK::Save(const QString &filename, const Item *itemsave){
    _check_connection();
    _send_Line("Save");
    _send_Line(filename);
    _send_Item(itemsave);
    _check_status();
}

/// <summary>
///     Adds a shape provided triangle coordinates. Triangles must be provided as a list of vertices. A vertex normal can
///     be provided optionally.
/// </summary>
/// <param name="trianglePoints">
///     List of vertices grouped by triangles (3xN or 6xN matrix, N must be multiple of 3 because
///     vertices must be stacked by groups of 3)
/// </param>
/// <param name="addTo">item to attach the newly added geometry (optional). Leave empty to create a new object.</param>
/// <param name="shapeOverride">Set to true to replace any other existing geometry</param>
/// <param name="color">Color of the added shape</param>
/// <returns>added object/shape (use item.Valid() to check if item is valid.)</returns>
Item RoboDK::AddShape(Mat *trianglePoints, Item *addTo, bool shapeOverride, Color *color)
{
    double colorArray[4] = {color->r,color->g,color->b,color->a};
    _check_connection();
    _send_Line("AddShape3");
    _send_Array(trianglePoints);
    _send_Item(addTo);
    _send_Int(shapeOverride? 1 : 0);
    _send_Array(colorArray,4);
    Item newitem = _recv_Item();
    _check_status();
    return newitem;
}

/// <summary>
/// Adds a curve provided point coordinates.
/// The provided points must be a list of vertices.
/// A vertex normal can be provided optionally.
/// </summary>
/// <param name="curvePoints">matrix 3xN or 6xN -> N must be multiple of 3</param>
/// <param name="referenceObject">object to add the curve and/or project the curve to the surface</param>
/// <param name="addToRef">
///     If True, the curve will be added as part of the object in the RoboDK item tree (a reference
///     object must be provided)
/// </param>
/// <param name="projectionType">
///     Type of projection. For example:  ProjectionType.AlongNormalRecalc will project along the
///     point normal and recalculate the normal vector on the surface projected.
/// </param>
/// <returns>added object/curve (use item.Valid() to check if item is valid.)</returns>
Item RoboDK::AddCurve(Mat *curvePoints, Item *referenceObject, bool addToRef, int ProjectionType)
{
    _check_connection();
    _send_Line("AddWire");
    _send_Array(curvePoints);
    _send_Item(referenceObject);
    _send_Int(addToRef ? 1:0);
    _send_Int(ProjectionType);
    Item newitem = _recv_Item();
    _check_status();
    return newitem;
}

/// <summary>
/// Adds a list of points to an object. The provided points must be a list of vertices. A vertex normal can be provided optionally.
/// </summary>
/// <param name="points">list of points as a matrix (3xN matrix, or 6xN to provide point normals as ijk vectors)</param>
/// <param name="referenceObject">item to attach the newly added geometry (optional)</param>
/// <param name="addToRef">If True, the points will be added as part of the object in the RoboDK item tree (a reference object must be provided)</param>
/// <param name="projectionType">Type of projection.Use the PROJECTION_* flags.</param>
/// <returns>added object/shape (0 if failed)</returns>
Item RoboDK::AddPoints(Mat *points, Item *referenceObject, bool addToRef, int ProjectionType)
{
    _check_connection();
    _send_Line("AddPoints");
    _send_Array(points);
    _send_Item(referenceObject);
    _send_Int(addToRef? 1 : 0);
    _send_Int(ProjectionType);Item newitem = _recv_Item();
    _check_status();
    return newitem;
}

Mat RoboDK::ProjectPoints(Mat *points, Item objectProject, int ProjectionType)
{
    _check_connection();
    _send_Line("ProjectPoints");
    _send_Array(points);
    _send_Item(objectProject);
    _send_Int(ProjectionType);
    Mat projectedPoints = _recv_Pose();
    _check_status();
    return projectedPoints;
}

/// <summary>
/// Add a new empty station.
/// </summary>
/// <param name="name">Name of the station</param>
/// <returns>Newly created station IItem</returns>
Item RoboDK::AddStation(QString name)
{
    _check_connection();
    _send_Line("NewStation");
    Item newitem = _recv_Item();
    _check_status();
    return newitem;
}




/// <summary>
/// Closes the current station without suggesting to save
/// </summary>
void RoboDK::CloseStation(){
    _check_connection();
    _send_Line("Remove");
    _send_Item(new Item(this));
    _check_status();
}

/// <summary>
/// Adds a new target that can be reached with a robot.
/// </summary>
/// <param name="name">name of the target</param>
/// <param name="itemparent">parent to attach to (such as a frame)</param>
/// <param name="itemrobot">main robot that will be used to go to self target</param>
/// <returns>the new target created</returns>
Item RoboDK::AddTarget(const QString &name, Item *itemparent, Item *itemrobot){
    _check_connection();
    _send_Line("Add_TARGET");
    _send_Line(name);
    _send_Item(itemparent);
    _send_Item(itemrobot);
    Item newitem = _recv_Item();
    _check_status();
    return newitem;
}

/// <summary>
/// Adds a new Frame that can be referenced by a robot.
/// </summary>
/// <param name="name">name of the reference frame</param>
/// <param name="itemparent">parent to attach to (such as the robot base frame)</param>
/// <returns>the new reference frame created</returns>
Item RoboDK::AddFrame(const QString &name, Item *itemparent){
    _check_connection();
    _send_Line("Add_FRAME");
    _send_Line(name);
    _send_Item(itemparent);
    Item newitem = _recv_Item();
    _check_status();
    return newitem;
}

/// <summary>
/// Adds a new Frame that can be referenced by a robot.
/// </summary>
/// <param name="name">name of the program</param>
/// <param name="itemparent">robot that will be used</param>
/// <returns>the new program created</returns>
Item RoboDK::AddProgram(const QString &name, Item *itemrobot){
    _check_connection();
    _send_Line("Add_PROG");
    _send_Line(name);
    _send_Item(itemrobot);
    Item newitem = _recv_Item();
    _check_status();
    return newitem;
}

/// <summary>
/// Add a new robot machining project. Machining projects can also be used for 3D printing, following curves and following points.
/// It returns the newly created :class:`.IItem` containing the project settings.
/// Tip: Use the macro /RoboDK/Library/Macros/MoveRobotThroughLine.py to see an example that creates a new "curve follow project" given a list of points to follow(Option 4).
/// </summary>
/// <param name="name">Name of the project settings</param>
/// <param name="itemrobot">Robot to use for the project settings(optional). It is not required to specify the robot if only one robot or mechanism is available in the RoboDK station.</param>
/// <returns></returns>
Item RoboDK::AddMachiningProject(QString name, Item *itemrobot)
{
    _check_connection();
    _send_Line("Add_MACHINING");
    _send_Line(name);
    _send_Item(itemrobot);
    Item newitem = _recv_Item();
    _check_status();
    return newitem;
}



QList<Item> RoboDK::getOpenStation()
{
    _check_connection();
    _send_Line("G_AllStn");
    int nstn = _recv_Int();
    QList<Item> *listStn = new QList<Item>();
    for (int i = 0;i < nstn;i++){
        Item station = _recv_Item();
        listStn->append(station);
    }
    _check_status();
    return *listStn;
}


/// <summary>
/// Returns the active station item (station currently visible)
/// </summary>
/// <returns></returns>
Item RoboDK::getActiveStation() {
    _check_connection();
    _send_Line("G_ActiveStn");
    Item station = _recv_Item();
    _check_status();
    return station;
}

/// <summary>
/// Set the active station (project currently visible)
/// </summary>
/// <param name="station">station item, it can be previously loaded as an RDK file</param>
void RoboDK::setActiveStation(Item station) {
    _check_connection();
    _send_Line("S_ActiveStn");
    _send_Item(station);
    _check_status();
}


//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
/// <summary>
/// Adds a function call in the program output. RoboDK will handle the syntax when the code is generated for a specific robot. If the program exists it will also run the program in simulate mode.
/// </summary>
/// <param name="function_w_params">Function name with parameters (if any)</param>
/// <returns></returns>
int RoboDK::RunProgram(const QString &function_w_params){
    return RunCode(function_w_params, true);
}

/// <summary>
/// Adds code to run in the program output. If the program exists it will also run the program in simulate mode.
/// </summary>
/// <param name="code"></param>
/// <param name="code_is_fcn_call"></param>
/// <returns></returns>
int RoboDK::RunCode(const QString &code, bool code_is_fcn_call){
    _check_connection();
    _send_Line("RunCode");
    _send_Int(code_is_fcn_call ? 1 : 0);
    _send_Line(code);
    int prog_status = _recv_Int();
    _check_status();
    return prog_status;
}

/// <summary>
/// Shows a message or a comment in the output robot program.
/// </summary>
/// <param name="message"></param>
/// <param name="message_is_comment"></param>
void RoboDK::RunMessage(const QString &message, bool message_is_comment){
    _check_connection();
    _send_Line("RunMessage");
    _send_Int(message_is_comment ? 1 : 0);
    _send_Line(message);
    _check_status();
}

/// <summary>
/// Renders the scene. This function turns off rendering unless always_render is set to true.
/// </summary>
/// <param name="always_render"></param>
void RoboDK::Render(bool always_render){
    bool auto_render = !always_render;
    _check_connection();
    _send_Line("Render");
    _send_Int(auto_render ? 1 : 0);
    _check_status();
}

/// <summary>
/// Update the screen.
/// This updates the position of all robots and internal links according to previously set values.
/// </summary>
void RoboDK::Update()
{
    _check_connection();
    _send_Line("Refresh");
    _send_Int(0);
    _check_status();
}

/// <summary>
/// Returns (1/True) if object_inside is inside the object_parent
/// </summary>
/// <param name="object_inside"></param>
/// <param name="object_parent"></param>
/// <returns></returns>
bool RoboDK::IsInside(Item object_inside, Item object_parent){
    _check_connection();
    _send_Line("IsInside");
    _send_Item(object_inside);
    _send_Item(object_parent);
    int inside = _recv_Int();
    _check_status();
    return inside > 0;
}

/// <summary>
/// Set collision checking ON or OFF (COLLISION_OFF/COLLISION_OFF) according to the collision map. If collision check is activated it returns the number of pairs of objects that are currently in a collision state.
/// </summary>
/// <param name="check_state"></param>
/// <returns>Number of pairs of objects in a collision state</returns>
int RoboDK::setCollisionActive(int check_state){
    _check_connection();
    _send_Line("Collision_SetState");
    _send_Int(check_state);
    int ncollisions = _recv_Int();
    _check_status();
    return ncollisions;
}

/// <summary>
/// Set collision checking ON or OFF (COLLISION_ON/COLLISION_OFF) for a specific pair of objects. This allows altering the collision map for Collision checking.
/// Specify the link id for robots or moving mechanisms (id 0 is the base).
/// </summary>
/// <param name="check_state">Set to COLLISION_ON or COLLISION_OFF</param>
/// <param name="item1">Item 1</param>
/// <param name="item2">Item 2</param>
/// <param name="id1">Joint id for Item 1 (if Item 1 is a robot or a mechanism)</param>
/// <param name="id2">Joint id for Item 2 (if Item 2 is a robot or a mechanism)</param>
/// <returns>Returns true if succeeded. Returns false if setting the pair failed (wrong id was provided)</returns>
bool RoboDK::setCollisionActivePair(int check_state, Item item1, Item item2, int id1, int id2){
    _check_connection();
    _send_Line("Collision_SetPair");
    _send_Item(item1);
    _send_Item(item2);
    _send_Int(id1);
    _send_Int(id2);
    _send_Int(check_state);
    int success = _recv_Int();
    _check_status();
    return success > 0;
}

/// <summary>
/// Returns the number of pairs of objects that are currently in a collision state.
/// </summary>
/// <returns></returns>
int RoboDK::Collisions(){
    _check_connection();
    _send_Line("Collisions");
    int ncollisions = _recv_Int();
    _check_status();
    return ncollisions;
}

/// <summary>
/// Returns 1 if item1 and item2 collided. Otherwise returns 0.
/// </summary>
/// <param name="item1"></param>
/// <param name="item2"></param>
/// <returns></returns>
int RoboDK::Collision(Item item1, Item item2){
    _check_connection();
    _send_Line("Collided");
    _send_Item(item1);
    _send_Item(item2);
    int ncollisions = _recv_Int();
    _check_status();
    return ncollisions;
}

QList<Item> RoboDK::getCollisionItems(QList<int> link_id_list)
{
    _check_connection();
    _send_Line("Collision Items");
    int nitems = _recv_Int();
    QList<Item> itemList = QList<Item>();
    if (!link_id_list.isEmpty()){
        link_id_list.clear();
    }
    for (int i = 0; i < nitems; i++){
        itemList.append(_recv_Item());
        int linkId = _recv_Int();
        if (!link_id_list.isEmpty()){
            link_id_list.append(linkId);
        }
        int collisionTimes = _recv_Int();
    }
    _check_status();
    return itemList;
}

/// <summary>
/// Sets the current simulation speed. Set the speed to 1 for a real-time simulation. The slowest speed allowed is 0.001 times the real speed. Set to a high value (>100) for fast simulation results.
/// </summary>
/// <param name="speed"></param>
void RoboDK::setSimulationSpeed(double speed){
    _check_connection();
    _send_Line("SimulateSpeed");
    _send_Int((int)(speed * 1000.0));
    _check_status();
}

/// <summary>
/// Gets the current simulation speed. Set the speed to 1 for a real-time simulation.
/// </summary>
/// <returns></returns>
double RoboDK::SimulationSpeed(){
    _check_connection();
    _send_Line("GetSimulateSpeed");
    double speed = ((double)_recv_Int()) / 1000.0;
    _check_status();
    return speed;
}
/// <summary>
/// Sets the behavior of the RoboDK API. By default, robodk shows the path simulation for movement instructions (run_mode=1=RUNMODE_SIMULATE).
/// Setting the run_mode to RUNMODE_QUICKVALIDATE allows performing a quick check to see if the path is feasible.
/// if robot.Connect() is used, RUNMODE_RUN_FROM_PC is selected automatically.
/// </summary>
/// <param name="run_mode">int = RUNMODE
/// RUNMODE_SIMULATE=1        performs the simulation moving the robot (default)
/// RUNMODE_QUICKVALIDATE=2   performs a quick check to validate the robot movements
/// RUNMODE_MAKE_ROBOTPROG=3  makes the robot program
/// RUNMODE_RUN_REAL=4        moves the real robot is it is connected</param>
void RoboDK::setRunMode(int run_mode){
    _check_connection();
    _send_Line("S_RunMode");
    _send_Int(run_mode);
    _check_status();
}

/// <summary>
/// Returns the behavior of the RoboDK API. By default, robodk shows the path simulation for movement instructions (run_mode=1)
/// </summary>
/// <returns>int = RUNMODE
/// RUNMODE_SIMULATE=1        performs the simulation moving the robot (default)
/// RUNMODE_QUICKVALIDATE=2   performs a quick check to validate the robot movements
/// RUNMODE_MAKE_ROBOTPROG=3  makes the robot program
/// RUNMODE_RUN_REAL=4        moves the real robot is it is connected</returns>
int RoboDK::RunMode(){
    _check_connection();
    _send_Line("G_RunMode");
    int runmode = _recv_Int();
    _check_status();
    return runmode;
}
/// <summary>
/// Gets all the user parameters from the open RoboDK station.
/// The parameters can also be modified by right clicking the station and selecting "shared parameters"
/// User parameters can be added or modified by the user
/// </summary>
/// <returns>list of pairs of strings as parameter-value (list of a list)</returns>
QList<QPair<QString,QString>> RoboDK::getParams(){
    _check_connection();
    _send_Line("G_Params");
    QList<QPair<QString,QString>> paramlist;
    int nparam = _recv_Int();
    for (int i = 0; i < nparam; i++) {
        QString param = _recv_Line();
        QString value = _recv_Line();
        paramlist.append(qMakePair(param, value));
    }
    _check_status();
    return paramlist;
}

/// <summary>
/// Gets a global or a user parameter from the open RoboDK station.
/// The parameters can also be modified by right clicking the station and selecting "shared parameters"
/// Some available parameters:
/// PATH_OPENSTATION = folder path of the current .stn file
/// FILE_OPENSTATION = file path of the current .stn file
/// PATH_DESKTOP = folder path of the user's folder
/// Other parameters can be added or modified by the user
/// </summary>
/// <param name="param">RoboDK parameter</param>
/// <returns>value</returns>
QString RoboDK::getParam(const QString &param){
    _check_connection();
    _send_Line("G_Param");
    _send_Line(param);
    QString value = _recv_Line();
    if (value.startsWith("UNKNOWN ")) {
        value = "";
    }
    _check_status();
    return value;
}

/// <summary>
/// Sets a global parameter from the RoboDK station. If the parameters exists, it will be modified. If not, it will be added to the station.
/// The parameters can also be modified by right clicking the station and selecting "shared parameters"
/// </summary>
/// <param name="param">RoboDK parameter</param>
/// <param name="value">value</param>
/// <returns></returns>
void RoboDK::setParam(const QString &param, const QString &value){
    _check_connection();
    _send_Line("S_Param");
    _send_Line(param);
    _send_Line(value);
    _check_status();
}

/// <summary>
/// Send a special command. These commands are meant to have a specific effect in RoboDK, such as changing a specific setting or provoke specific events.
/// </summary>
/// <param name="cmd">Command Name, such as Trace, Threads or Window.</param>
/// <param name="value">Comand value (optional, not all commands require a value)</param>
/// <returns></returns>
QString RoboDK::Command(const QString &cmd, const QString &value){
    _check_connection();
    _send_Line("SCMD");
    _send_Line(cmd);
    _send_Line(value);
    QString answer = _recv_Line();
    _check_status();
    return answer;
}

bool RoboDK::LaserTrackerMeasure(tXYZ xyz, tXYZ estimate, bool search)
{
    _check_connection();
    _send_Line("MeasLT");
    _send_XYZ(estimate);
    _send_Int(search ? 1 : 0);
    _recv_XYZ(xyz);
    _check_status();
    if (xyz[0]*xyz[0] + xyz[1]*xyz[1] + xyz[2]*xyz[2] < 0.0001){
        return false;
    }

    return true;
}

void RoboDK::ShowAsCollided(QList<Item> itemList, QList<bool> collidedList, QList<int> *robot_link_id)
{
    int nitems = qMin(itemList.length(),collidedList.length());
    if (robot_link_id != NULL){
        nitems = qMin(nitems, robot_link_id->length());
    }
    _check_connection();
    _send_Line("ShowAsCollidedList");
    _send_Int(nitems);
    for (int i = 0; i < nitems; i++){
        _send_Item(itemList[i]);
        _send_Int(collidedList[i] ? 1 : 0);
        int link_id = 0;
        if (robot_link_id != NULL){
            link_id = robot_link_id->at(i);
        }
        _send_Int(link_id);
    }
    _check_status();
}

//---------------------------------------------- ADD MORE  (getParams, setParams, calibrate TCP, calibrate ref...)


/// <summary>
/// Calibrate a tool (TCP) given a number of points or calibration joints. Important: If the robot is calibrated, provide joint values to maximize accuracy.
/// </summary>
/// <param name="poses_joints">matrix of poses in a given format or a list of joints</param>
/// <param name="error_stats">stats[mean, standard deviation, max] - Output error stats summary</param>
/// <param name="format">Euler format. Optionally, use JOINT_FORMAT and provide the robot.</param>
/// <param name="algorithm">type of algorithm (by point, plane, ...)</param>
/// <param name="robot">Robot used for calibration (if using joint values)</param>
/// <returns>TCP as [x, y, z] - calculated TCP</returns>
///
void RoboDK::CalibrateTool(tMatrix2D *poses_joints, tXYZ tcp_xyz, int format, int algorithm, Item *robot, double *error_stats){
    _check_connection();
    _send_Line("CalibTCP2");
    _send_Matrix2D(poses_joints);
    _send_Int(format);
    _send_Int(algorithm);
    _send_Item(robot);
    int nxyz = 3;
    _recv_Array(tcp_xyz, &nxyz);
    if (error_stats != NULL){
        _recv_Array(error_stats);
    } else {
        double errors_ignored[20];
        _recv_Array(errors_ignored);
    }
    tMatrix2D *error_graph = Matrix2D_Create();
    _recv_Matrix2D(&error_graph);
    Matrix2D_Delete(&error_graph);
    _check_status();
}

/// <summary>
/// Calibrate a Reference Frame given a list of points or joint values. Important: If the robot is calibrated, provide joint values to maximize accuracy.
/// </summary>
/// <param name="joints">points as a 3xN matrix or nDOFsxN) - List of points or a list of robot joints</param>
/// <param name="method">type of algorithm(by point, plane, ...) CALIBRATE_FRAME_...</param>
/// <param name="use_joints">use points or joint values. The robot item must be provided if joint values is used.</param>
/// <param name="robot"></param>
/// <returns></returns>
Mat RoboDK::CalibrateReference(tMatrix2D *poses_joints, int method, bool use_joints, Item *robot){
    _check_connection();
    _send_Line("CalibFrame");
    _send_Matrix2D(poses_joints);
    _send_Int(use_joints ? -1 : 0);
    _send_Int(method);
    _send_Item(robot);
    Mat reference_pose = _recv_Pose();
    double error_stats[20];
    _recv_Array(error_stats);
    _check_status();
    return reference_pose;
}


int RoboDK::ProgramStart(const QString &progname, const QString &defaultfolder, const QString &postprocessor, Item *robot){
    _check_connection();
    _send_Line("ProgramStart");
    _send_Line(progname);
    _send_Line(defaultfolder);
    _send_Line(postprocessor);
    _send_Item(robot);
    int errors = _recv_Int();
    _check_status();
    return errors;
}

/// <summary>
/// Set the pose of the wold reference frame with respect to the view (camera/screen)
/// </summary>
/// <param name="pose"></param>
void RoboDK::setViewPose(const Mat &pose){
    _check_connection();
    _send_Line("S_ViewPose");
    _send_Pose(pose);
    _check_status();
}

/// <summary>
/// Get the pose of the wold reference frame with respect to the view (camera/screen)
/// </summary>
/// <param name="pose"></param>
Mat RoboDK::ViewPose(){
    _check_connection();
    _send_Line("G_ViewPose");
    Mat pose = _recv_Pose();
    _check_status();
    return pose;
}
//INCOMPLETE!!!!!!!
/*bool RoboDK::SetRobotParams(Item *robot, tMatrix2D dhm, Mat poseBase, Mat poseTool)
{
    _check_connection();
    _send_Line("S_AbsAccParam");
    _send_Item(robot);
    Mat r2b;
    r2b.setToIdentity();
    _send_Pose(r2b);
    _send_Pose(poseBase);
    _send_Pose(poseTool);
    int *ndofs = dhm.size;
    _send_Int(*ndofs);
    for (int i = 0; i < *ndofs; i++){
        _send_Array(dhm);
    }

    _send_Pose(poseBase);
    _send_Pose(poseTool);
    _send_Int(*ndofs);
    for (int i = 0; i < *ndofs; i++){
        _send_Array(dhm[i]);
    }

    _send_Array(NULL);
    _send_Array(NULL);
    _check_status();
    return true;
}*/

Item RoboDK::getCursorXYZ(int x, int y, tXYZ xyzStation)
{
    _check_connection();
    _send_Line("Proj2d3d");
    _send_Int(x);
    _send_Int(y);
    int selection = _recv_Int();
    Item selectedItem = _recv_Item();
    tXYZ xyz;
    _recv_XYZ(xyz);
    if (xyzStation != NULL){
        xyzStation[0] = xyz[0];
        xyzStation[1] = xyz[1];
        xyzStation[2] = xyz[2];
    }
    _check_status();
    return selectedItem;
}


///---------------------------------- add get/set robot parameters


/// <summary>
/// Returns the license string (as shown in the RoboDK main window)
/// </summary>
/// <returns></returns>
QString RoboDK::License(){
    _check_connection();
    _send_Line("G_License");
    QString license = _recv_Line();
    _check_status();
    return license;
}

/// <summary>
/// Returns the list of items selected (it can be one or more items)
/// </summary>
/// <returns></returns>
QList<Item> RoboDK::Selection(){
    _check_connection();
    _send_Line("G_Selection");
    int nitems = _recv_Int();
    QList<Item> list_items;
    for (int i = 0; i < nitems; i++)    {
        list_items.append(_recv_Item());
    }
    _check_status();
    return list_items;
}


/// <summary>
/// Show the popup menu to create the ISO9283 path for path accuracy and performance testing
/// </summary>
/// <returns>IS9283 Program</returns>
Item RoboDK::Popup_ISO9283_CubeProgram(Item *robot, tXYZ center, double side, bool blocking){
    //_require_build(5177);
    Item iso_program;
    _check_connection();
    if (center == NULL){
        _send_Line("Popup_ProgISO9283");
        _send_Item(robot);
        _TIMEOUT = 3600 * 1000;
        iso_program = _recv_Item();
        _TIMEOUT = ROBODK_API_TIMEOUT;
        _check_status();
    } else {
        _send_Line("Popup_ProgISO9283_Param");
        _send_Item(robot);
        double values[5];
        values[0] = center[0];
        values[1] = center[1];
        values[2] = center[2];
        values[3] = side;
        values[4] = blocking ? 1 : 0;
        _send_Array(values, 4);
        if (blocking){
            _TIMEOUT = 3600 * 1000;
            iso_program = _recv_Item();
            _TIMEOUT = ROBODK_API_TIMEOUT;
            _check_status();
        }
    }
    return iso_program;
}



//-------------------------- private ---------------------------------------

bool RoboDK::_connected(){
    return _COM != NULL && _COM->state() == QTcpSocket::ConnectedState;
}


bool RoboDK::_check_connection(){
    if (_connected()){
        return true;
    }
    bool connection_ok = _connect_smart();
    //if (!connection_ok){
    //    throw -1;
    //}
    return connection_ok;
}

bool RoboDK::_check_status(){
    qint32 status = _recv_Int();
    if (status > 0 && status < 10) {
        QString strproblems("Unknown error");
        if (status == 1) {
            strproblems = "Invalid item provided: The item identifier provided is not valid or it does not exist.";
        } else if (status == 2) { //output warning only
            strproblems = _recv_Line();
            qDebug() << "RoboDK API WARNING: " << strproblems;
            return 0;
        } else if (status == 3){ // output error
            strproblems = _recv_Line();
            qDebug() << "RoboDK API ERROR: " << strproblems;
        } else if (status == 9) {
            qDebug() << "Invalid RoboDK License";
        }
        //print(strproblems);
        //throw new RDKException(strproblems); //raise Exception(strproblems)
    } else if (status == 0) {
        // everything is OK
        //status = status
    } else  {
        //throw new RDKException("Communication problems with the RoboDK API"); //raise Exception('Problems running function');
        qDebug() << "Communication problems with the RoboDK API";
    }
    return status;
}



void RoboDK::_disconnect(){
    if (_COM != NULL){
        _COM->deleteLater();
        _COM = NULL;
    }
}

// attempt a simple connection to RoboDK and start RoboDK if it is not running
bool RoboDK::_connect_smart(){
    //Establishes a connection with robodk. robodk must be running, otherwise, it will attempt to start it
    if (_connect()){
        qDebug() << "The RoboDK API is connected";
        return true;
    }

    qDebug() << "...Trying to start RoboDK: " << _ROBODK_BIN << " " << _ARGUMENTS;
    // Start RoboDK
    QProcess *p = new QProcess();
    //_ARGUMENTS = "/DEBUG";
    p->start(_ROBODK_BIN, _ARGUMENTS.split(" ", QString::SkipEmptyParts));
    p->setReadChannel(QProcess::StandardOutput);
    //p->waitForReadyRead(10000);
    _PROCESS = p->processId();
    while (p->canReadLine() || p->waitForReadyRead(5000)){
        QString line = QString::fromUtf8(p->readLine().trimmed());
        //qDebug() << "RoboDK process: " << line;
        if (line.contains("Running", Qt::CaseInsensitive)){
            qDebug() << "RoboDK is Running... Connecting API";
            bool is_connected = _connect();
            if (is_connected){
                qDebug() << "The RoboDK API is connected";
            } else {
                qDebug() << "The RoboDK API is NOT connected!";
            }
            return is_connected;
        }
    }
    qDebug() << "Could not start RoboDK!";
    return false;
}

// attempt a simple connection to RoboDK
bool RoboDK::_connect(){
    _disconnect();
    _COM = new QTcpSocket();
    if (_IP.isEmpty()){
        _COM->connectToHost("127.0.0.1", _PORT); //QHostAddress::LocalHost, _PORT);
    } else {
        _COM->connectToHost(_IP, _PORT);
    }
    // usually, 5 msec should be enough for localhost
    if (!_COM->waitForConnected(_TIMEOUT)){
        _COM->deleteLater();
        _COM = NULL;
        return false;
    }

    // RoboDK protocol to check that we are connected to the right port
    _COM->write(ROBODK_API_START_STRING); _COM->write(ROBODK_API_LF, 1);
    _COM->write("1 0"); _COM->write(ROBODK_API_LF, 1);

    // 5 msec should be enough for localhost
    /*if (!_COM->waitForBytesWritten(_TIMEOUT)){
        _COM->deleteLater();
        _COM = NULL;
        return false;
    }*/
    // 10 msec should be enough for localhost
    if (!_COM->canReadLine() && !_COM->waitForReadyRead(_TIMEOUT)){
        _COM->deleteLater();
        _COM = NULL;
        return false;
    }
    QString read(_COM->readAll());
    // make sure we receive the OK from RoboDK
    if (!read.startsWith(ROBODK_API_READY_STRING)){
        _COM->deleteLater();
        _COM = NULL;
        return false;
    }
    return true;
}


/////////////////////////////////////////////
bool RoboDK::_waitline(){
    if (_COM == NULL){ return false; }
    while (!_COM->canReadLine()){
        if (!_COM->waitForReadyRead(_TIMEOUT)){
            return false;
        }
    }
    return true;
}
QString RoboDK::_recv_Line(){//QString &string){
    QString string;
    if (!_waitline()){
        if (_COM != NULL){
            //if this happens it means that there are problems: delete buffer
            _COM->readAll();
        }
        return string;
    }
    QByteArray line = _COM->readLine().trimmed();//remove last character \n //.trimmed();
    string.append(QString::fromUtf8(line));
    return string;
}
bool RoboDK::_send_Line(const QString& string){
    if (_COM == NULL || !_COM->isOpen()){ return false; }
    _COM->write(string.toUtf8());
    _COM->write(ROBODK_API_LF, 1);
    return true;
}

int RoboDK::_recv_Int(){//qint32 &value){
    qint32 value; // do not change type
    if (_COM == NULL){ return false; }
    if (_COM->bytesAvailable() < sizeof(qint32)){
        _COM->waitForReadyRead(_TIMEOUT);
        if (_COM->bytesAvailable() < sizeof(qint32)){
            return -1;
        }
    }
    QDataStream ds(_COM);
    ds >> value;
    return value;
}
bool RoboDK::_send_Int(qint32 value){
    if (_COM == NULL || !_COM->isOpen()){ return false; }
    QDataStream ds(_COM);
    ds << value;
    return true;
}

Item RoboDK::_recv_Item(){//Item *item){
    Item item(this);
    if (_COM == NULL){ return item; }
    item._PTR = 0;
    item._TYPE = -1;
    if (_COM->bytesAvailable() < sizeof(quint64)){
        _COM->waitForReadyRead(_TIMEOUT);
        if (_COM->bytesAvailable() < sizeof(quint64)){
            return item;
        }
    }
    QDataStream ds(_COM);
    ds >> item._PTR;
    ds >> item._TYPE;
    return item;
}
bool RoboDK::_send_Item(const Item *item){
    if (_COM == NULL || !_COM->isOpen()){ return false; }
    QDataStream ds(_COM);
    quint64 ptr = 0;
    if (item != NULL){
        ptr = item->_PTR;
    }
    ds << ptr;
    return true;
}
bool RoboDK::_send_Item(const Item &item){
    return _send_Item(&item);
}

Mat RoboDK::_recv_Pose(){//Mat &pose){
    Mat pose;
    if (_COM == NULL){ return pose; }
    int size = 16*sizeof(double);
    if (_COM->bytesAvailable() < size){
        _COM->waitForReadyRead(_TIMEOUT);
        if (_COM->bytesAvailable() < size){
            return pose;
        }
    }
    QDataStream ds(_COM);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    //ds.setByteOrder(QDataStream::LittleEndian);
    double valuei;
    for (int j=0; j<4; j++){
        for (int i=0; i<4; i++){
            ds >> valuei;
            pose.Set(i,j,valuei);
            //pose.data()[i][j] = valuei;
        }
    }
    return pose;
}
bool RoboDK::_send_Pose(const Mat &pose){
    if (_COM == NULL || !_COM->isOpen()){ return false; }
    QDataStream ds(_COM);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    //ds.setByteOrder(QDataStream::LittleEndian);
    double valuei;    
    for (int j=0; j<4; j++){
        for (int i=0; i<4; i++){
            valuei = pose.Get(i,j);
            ds << valuei;
        }
    }
    return true;
}
bool RoboDK::_recv_XYZ(tXYZ pos){
    if (_COM == NULL){ return false; }
    int size = 3*sizeof(double);
    if (_COM->bytesAvailable() < size){
        _COM->waitForReadyRead(_TIMEOUT);
        if (_COM->bytesAvailable() < size){
            return false;
        }
    }
    QDataStream ds(_COM);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    //ds.setByteOrder(QDataStream::LittleEndian);
    double valuei;
    for (int i=0; i<3; i++){
        ds >> valuei;
        pos[i] = valuei;
    }
    return true;
}
bool RoboDK::_send_XYZ(const tXYZ pos){
    if (_COM == NULL || !_COM->isOpen()){ return false; }
    QDataStream ds(_COM);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    //ds.setByteOrder(QDataStream::LittleEndian);
    double valuei;
    for (int i=0; i<3; i++){
        valuei = pos[i];
        ds << valuei;
    }
    return true;
}
bool RoboDK::_recv_Array(tJoints *jnts){
    return _recv_Array(jnts->Values, &(jnts->nDOFs));
}
bool RoboDK::_send_Array(const tJoints *jnts){
    if (jnts == NULL){
        return _send_Int(0);
    }
    return _send_Array(jnts->Values, jnts->nDOFs);
}
bool RoboDK::_send_Array(const Mat *mat){
    if (mat == NULL){
        return _send_Int(0);
    }
    double m44[16];
    for (int c=0; c<4; c++){
        for (int r=0; r<4; r++){
            m44[c*4+r] = mat->Get(r,c);
        }
    }
    return _send_Array(m44, 16);
}
bool RoboDK::_recv_Array(double *values, int *psize){
    int nvalues = _recv_Int();
    if (_COM == NULL || nvalues < 0) {return false;}
    if (psize != NULL){
        *psize = nvalues;
    }
    if (nvalues < 0 || nvalues > 50){return false;} //check if the value is not too big
    int size = nvalues*sizeof(double);
    if (_COM->bytesAvailable() < size){
        _COM->waitForReadyRead(_TIMEOUT);
        if (_COM->bytesAvailable() < size){
            return false;
        }
    }
    QDataStream ds(_COM);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    //ds.setByteOrder(QDataStream::LittleEndian);
    double valuei;
    for (int i=0; i<nvalues; i++){
        ds >> valuei;
        values[i] = valuei;
    }
    return true;
}
bool RoboDK::_send_Array(const double *values, int nvalues){
    if (_COM == NULL || !_COM->isOpen()){ return false; }
    if (!_send_Int((qint32)nvalues)){ return false; }
    QDataStream ds(_COM);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    //ds.setByteOrder(QDataStream::LittleEndian);
    double valuei;
    for (int i=0; i<nvalues; i++){
        valuei = values[i];
        ds << valuei;
    }
    return true;
}
bool RoboDK::_recv_Matrix2D(tMatrix2D **mat){ // needs to delete after!
    qint32 dim1 = _recv_Int();
    qint32 dim2 = _recv_Int();
    *mat = Matrix2D_Create();
    //emxInit_real_T(mat, 2);
    if (dim1 < 0 || dim2 < 0){ return false; }
    Matrix2D_Set_Size(*mat, dim1, dim2);
    if (dim1*dim2 <= 0){
        return true;
    }
    QByteArray buffer;
    int count = 0;
    double value;
    while (true){
        int remaining = dim1*dim2 - count;
        if (remaining <= 0){ return true; }
        if (_COM->bytesAvailable() <= 0 && !_COM->waitForReadyRead(_TIMEOUT)){
            Matrix2D_Delete(mat);
            return false;
        }
        buffer = _COM->read(remaining*sizeof(double));
        QDataStream indata(buffer);
        indata.setFloatingPointPrecision(QDataStream::DoublePrecision);
        while (!indata.atEnd()){
            indata >> value;
            (*mat)->data[count] = value;
            count = count + 1;
        }
    }
    return false;// we should never arrive here...
}
bool RoboDK::_send_Matrix2D(tMatrix2D *mat){
    if (_COM == NULL || !_COM->isOpen()){ return false; }
    QDataStream ds(_COM);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    //ds.setByteOrder(QDataStream::LittleEndian);
    qint32 dim1 = Matrix2D_Size(mat, 1);
    qint32 dim2 = Matrix2D_Size(mat, 2);
    bool ok1 = _send_Int(dim1);
    bool ok2 = _send_Int(dim2);
    if (!ok1 || !ok2) {return false; }
    double valuei;
    for (int j=0; j<dim2; j++){
        for (int i=0; i<dim1; i++){
            valuei = Matrix2D_Get_ij(mat, i, j);
            ds << valuei;
        }
    }
    return true;
}
// private move type, to be used by public methods (MoveJ  and MoveL)
void RoboDK::_moveX(const Item *target, const tJoints *joints, const Mat *mat_target, const Item *itemrobot, int movetype, bool blocking){
    itemrobot->WaitMove();
    _send_Line("MoveX");
    _send_Int(movetype);
    if (target != NULL){
        _send_Int(3);
        _send_Array((tJoints*)NULL);
        _send_Item(target);
    } else if (joints != NULL){
        _send_Int(1);
        _send_Array(joints);
        _send_Item((Item*)NULL);
    } else if (mat_target != NULL){// && mat_target.IsHomogeneous()) {
        _send_Int(2);
        _send_Array(mat_target); // keep it as array!
        _send_Item((Item*)NULL);
    } else {
        //throw new RDKException("Invalid target type"); //raise Exception('Problems running function');
        throw 0;
    }
    _send_Item(itemrobot);
    _check_status();
    if (blocking){
        itemrobot->WaitMove();
    }
}
// private move type, to be used by public methods (MoveJ  and MoveL)
void RoboDK::_moveC(const Item *target1, const tJoints *joints1, const Mat *mat_target1, const Item *target2, const tJoints *joints2, const Mat *mat_target2, const Item *itemrobot, bool blocking){
    itemrobot->WaitMove();
    _send_Line("MoveC");
    _send_Int(3);
    if (target1 != NULL){
        _send_Int(3);
        _send_Array((tJoints*)NULL);
        _send_Item(target1);
    } else if (joints1 != NULL) {
        _send_Int(1);
        _send_Array(joints1);
        _send_Item((Item*)NULL);
    } else if (mat_target1 != NULL){// && mat_target1.IsHomogeneous()) {
        _send_Int(2);
        _send_Array(mat_target1);
        _send_Item((Item*)NULL);
    } else {
        throw 0;
        //throw new RDKException("Invalid type of target 1");
    }
    /////////////////////////////////////
    if (target2 != NULL) {
        _send_Int(3);
        _send_Array((tJoints*)NULL);
        _send_Item(target2);
    } else if (joints2 != NULL) {
        _send_Int(1);
        _send_Array(joints2);
        _send_Item((Item*)NULL);
    } else if (mat_target2 != NULL){// && mat_target2.IsHomogeneous()) {
        _send_Int(2);
        _send_Array(mat_target2);
        _send_Item((Item*)NULL);
    } else {
        throw 0;
        //throw new RDKException("Invalid type of target 2");
    }
    /////////////////////////////////////
    _send_Item(itemrobot);
    _check_status();
    if (blocking){
        itemrobot->WaitMove();
    }
}













//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
/////////////////////////////////////
// 2D matrix functions
/////////////////////////////////////
void emxInit_real_T(tMatrix2D **pEmxArray, int numDimensions)
{
    tMatrix2D *emxArray;
    int i;
    *pEmxArray = (tMatrix2D *)malloc(sizeof(tMatrix2D));
    emxArray = *pEmxArray;
    emxArray->data = (double *)NULL;
    emxArray->numDimensions = numDimensions;
    emxArray->size = (int *)malloc((unsigned int)(sizeof(int) * numDimensions));
    emxArray->allocatedSize = 0;
    emxArray->canFreeData = true;
    for (i = 0; i < numDimensions; i++) {
        emxArray->size[i] = 0;
    }
}
///
tMatrix2D* Matrix2D_Create() {
	tMatrix2D *matrix;
    emxInit_real_T((tMatrix2D**)(&matrix), 2);
	return matrix;
}


void emxFree_real_T(tMatrix2D **pEmxArray){
    if (*pEmxArray != (tMatrix2D *)NULL) {
        if (((*pEmxArray)->data != (double *)NULL) && (*pEmxArray)->canFreeData) {
            free((void *)(*pEmxArray)->data);
        }
        free((void *)(*pEmxArray)->size);
        free((void *)*pEmxArray);
        *pEmxArray = (tMatrix2D *)NULL;
    }
}

void Matrix2D_Delete(tMatrix2D **mat) {
    emxFree_real_T((tMatrix2D**)(mat));
}



void emxEnsureCapacity(tMatrix2D *emxArray, int oldNumel, unsigned int elementSize){
    int newNumel;
    int i;
    double *newData;
    if (oldNumel < 0) {
        oldNumel = 0;
    }
    newNumel = 1;
    for (i = 0; i < emxArray->numDimensions; i++) {
        newNumel *= emxArray->size[i];
    }
    if (newNumel > emxArray->allocatedSize) {
        i = emxArray->allocatedSize;
        if (i < 16) {
            i = 16;
        }
        while (i < newNumel) {
            if (i > 1073741823) {
                i =(2147483647);//MAX_int32_T;
            } else {
                i <<= 1;
            }
        }
        newData = (double*) calloc((unsigned int)i, elementSize);
        if (emxArray->data != NULL) {
            memcpy(newData, emxArray->data, elementSize * oldNumel);
            if (emxArray->canFreeData) {
                free(emxArray->data);
            }
        }
        emxArray->data = newData;
        emxArray->allocatedSize = i;
        emxArray->canFreeData = true;
    }
}

void Matrix2D_Set_Size(tMatrix2D *mat, int rows, int cols) {
	int old_numel;
	int numbel;
	old_numel = mat->size[0] * mat->size[1];
	mat->size[0] = rows;
	mat->size[1] = cols;
	numbel = rows*cols;
    emxEnsureCapacity(mat, old_numel, sizeof(double));
	/*for (i=0; i<numbel; i++){
	mat->data[i] = 0.0;
	}*/
}

int Matrix2D_Size(const tMatrix2D *var, int dim) { // ONE BASED!!
	if (var->numDimensions >= dim) {
		return var->size[dim - 1];
	}
	else {
		return 0;
	}
}
int Matrix2D_Get_ncols(const tMatrix2D *var) {
    return Matrix2D_Size(var, 2);
}
int Matrix2D_Get_nrows(const tMatrix2D *var) {
    return Matrix2D_Size(var, 1);
}
double Matrix2D_Get_ij(const tMatrix2D *var, int i, int j) { // ZERO BASED!!
	return var->data[var->size[0] * j + i];
}
void Matrix2D_SET_ij(const tMatrix2D *var, int i, int j, double value) { // ZERO BASED!!
	var->data[var->size[0] * j + i] = value;
}

double *Matrix2D_Get_col(const tMatrix2D *var, int col) { // ZERO BASED!!
	return (var->data + var->size[0] * col);
}


void Matrix2D_Add(tMatrix2D *var, const double *array, int numel){
    int oldnumel;
    int size1 = var->size[0];
    int size2 = var->size[1];
    oldnumel = size1*size2;
    var->size[1] = size2 + 1;
    emxEnsureCapacity(var, oldnumel, (int)sizeof(double));
    numel = qMin(numel, size1);
    for (int i=0; i<numel; i++){
        var->data[size1*size2 + i] = array[i];
    }
}

void Matrix2D_Add(tMatrix2D *var, const tMatrix2D *varadd){
    int oldnumel;
    int size1 = var->size[0];
    int size2 = var->size[1];
    int size1_ap = varadd->size[0];
    int size2_ap = varadd->size[1];
    int numel = size1_ap*size2_ap;
    if (size1 != size1_ap){
        return;
    }
    oldnumel = size1*size2;
    var->size[1] = size2 + size2_ap;
    emxEnsureCapacity(var, oldnumel, (int)sizeof(double));
    for (int i=0; i<numel; i++){
        var->data[size1*size2 + i] = varadd->data[i];
    }
}

void Debug_Array(const double *array, int arraysize) {
	int i;
	for (i = 0; i < arraysize; i++) {
		//char chararray[500];  // You had better have room for what you are sprintf()ing!
		//sprintf(chararray, "%.3f", array[i]);
		//std::cout << chararray;
		printf("%.3f", array[i]);
		if (i < arraysize - 1) {
			//std::cout << " , ";
			printf(" , ");
		}
	}
}

void Debug_Matrix2D(const tMatrix2D *emx) {
	int size1;
	int size2;
	int j;
	double *column;
	size1 = Matrix2D_Get_nrows(emx);
	size2 = Matrix2D_Get_ncols(emx);
	printf("Matrix size = [%i, %i]\n", size1, size2);
	//std::out << "Matrix size = [%i, %i]" << size1 << " " << size2 << "]\n";
	for (j = 0; j<size2; j++) {
		column = Matrix2D_Get_col(emx, j);
		Debug_Array(column, size1);
		printf("\n");
		//std::cout << "\n";
	}
}
/*
void Debug_Mat(Mat pose, char show_full_pose) {
	tMatrix4x4 pose_tr;
	double xyzwpr[6];
	int j;
    if (show_full_pose > 0) {
		POSE_TR(pose_tr, pose);
		printf("Pose size = [4x4]\n");
		//std::cout << "Pose size = [4x4]\n";
		for (j = 0; j < 4; j++) {
			Debug_Array(pose_tr + j * 4, 4);
			printf("\n");
			//std::cout << "\n";
		}
	}
	else {
		POSE_2_XYZWPR(xyzwpr, pose);
		//std::cout << "XYZWPR = [ ";
		printf("XYZWPR = [ ");
		Debug_Array(xyzwpr, 6);
		printf(" ]\n");
		//std::cout << " ]\n";
    }
}
*/




#ifndef RDK_SKIP_NAMESPACE
}
#endif
