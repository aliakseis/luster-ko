#include "ScriptModel.h"

#include "datasingleton.h"
#include "makeguard.h"

#include "effects/scripteffect.h"
#include "effects/scripteffectwithsettings.h"

#include "PythonConsoleRedirector.h"

#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>
#include <QVariantMap>
#include <QVariantList>
#include <QImage>
#include <QAction>
#include <QMenu>
#include <QProcess>
#include <QApplication>
#include <QMessageBox>
#include <QThread>

#undef slots

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace py = pybind11;
using namespace py::literals;

namespace {

    // UI-safe async error dialog (invoked from any thread)
    void showErrorAsync(const QString& title, const QString& message) {
        QMetaObject::invokeMethod(
            qApp,
            [title, message]() {
                QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
                QWidget* parent = app ? app->activeWindow() : nullptr;
                QMessageBox::critical(parent, title, message);
            },
            Qt::QueuedConnection);
    }

    bool isPythonInstalled()
    {
        const int status =
            QProcess::execute(QCoreApplication::applicationFilePath(),
                QStringList() << CHECK_PYTHON_OPTION);

        return status == 0;
    }

    //------------------------------------------------------------------------------
// Converts a QImage to a contiguous pybind11::array (NumPy array).
// The function ensures the QImage is in Format_RGB888 (3-channel format).
    py::array qimage_to_nparray(const QImage& inImage) {
        // Convert image to a well-defined RGB format.
        QImage image = inImage.convertToFormat(QImage::Format_RGB888);

        int height = image.height();
        int width = image.width();
        constexpr int channels = 3; // RGB

        // Allocate a new contiguous NumPy array with shape (height, width, 3).
        py::array_t<uchar> arr({ height, width, channels });
        py::buffer_info buf = arr.request();
        uchar* dest = static_cast<uchar*>(buf.ptr);

        // Copy row-by-row to ensure a contiguous memory layout.
        for (int i = 0; i < height; i++) {
            const uchar* src = image.constScanLine(i);
            std::memcpy(dest + i * (width * channels), src, width * channels);
        }

        return arr;
    }

    uint8_t clip_uint8(long a) {
        const uint8_t noOverflowCandidate = static_cast<uint8_t>(a);
        return (noOverflowCandidate == a) ? noOverflowCandidate : ((noOverflowCandidate < a) ? UINT8_MAX : 0);
    }

    //------------------------------------------------------------------------------
    // Converts a NumPy array (pybind11::array) back into a QImage.
    // Accepts uint8 or float arrays arranged as HxWxC or CxHxW, with stride-aware fallback.
    QImage nparray_to_qimage(const py::array& a) {
        py::buffer_info info = a.request();

        // Accept either (H,W,C) where axis2 == 3, or (C,H,W) where axis0 == 3.
        // If neither holds, we still allow HWC but will throw below if shape mismatch.

        // Determine height/width/channels in a robust way.
        int height = static_cast<int>(info.shape[0]);
        int width = static_cast<int>(info.shape[1]);
        constexpr int channels = 3;

        QImage image(width, height, QImage::Format_RGB888);

        // Helper lambda to read a float element using strides.
        auto read_float_at = [&](int i, int j, int k) -> float {
            // offset in bytes
            const auto& strides = info.strides;
            const char* base = static_cast<const char*>(info.ptr);
            ptrdiff_t offset = 0;
            // info.strides contains stride per axis in bytes
            offset = i * strides[0] + j * strides[1] + k * strides[2];
            const float* p = reinterpret_cast<const float*>(base + offset);
            return *p;
            };

        if (info.format == py::format_descriptor<unsigned char>::format()) {

            const unsigned char* src = static_cast<const unsigned char*>(info.ptr);
            // If layout is H x W x C contiguous (common), copy straightforward.
            if (info.strides[0] == width * channels
                && info.strides[1] == channels
                && info.strides[2] == 1) {
                for (int i = 0; i < height; ++i) {
                    unsigned char* dest = image.scanLine(i);
                    std::memcpy(dest, src + i * (width * channels), static_cast<size_t>(width * channels));
                }
                return image;
            }

            // General stride-aware read for uint8
            for (int i = 0; i < height; ++i) {
                unsigned char* dest = image.scanLine(i);
                for (int j = 0; j < width; ++j) {
                    for (int k = 0; k < channels; ++k) {
                        // compute byte offset
                        ptrdiff_t off = i * info.strides[0] + j * info.strides[1] + k * info.strides[2];
                        const unsigned char* p = reinterpret_cast<const unsigned char*>(reinterpret_cast<const char*>(info.ptr) + off);
                        dest[j * channels + k] = *p;
                    }
                }
            }
            return image;
        }
        else if (info.format == py::format_descriptor<float>::format())
        {
            const float* src = static_cast<const float*>(info.ptr);
            // Determine whether array is channels-last (H,W,C) contiguous
            const bool is_hwc_contiguous =
                info.strides[2] == sizeof(float) &&
                info.strides[1] == channels * sizeof(float);

            // Determine channels-first (C,H,W) contiguous
            const bool is_chw_contiguous =
                info.strides[0] == width * height * sizeof(float) ||
                (info.shape[0] == channels && info.strides[1] == width * sizeof(float));

            if (is_hwc_contiguous) {
                // H x W x C contiguous (row-major)
                for (int i = 0; i < height; ++i) {
                    unsigned char* dest = image.scanLine(i);
                    for (int j = 0; j < width; ++j) {
                        for (int k = 0; k < channels; ++k) {
                            long scaled = std::lround(src[i * (width * channels) + j * channels + k] * 255.0f);
                            dest[j * channels + k] = clip_uint8(scaled);
                        }
                    }
                }
                return image;
            }
            else if (is_chw_contiguous) {
                // C x H x W layout (channel outermost) - preserve the indexing you validated.
                for (int i = 0; i < height; ++i) {
                    unsigned char* dest = image.scanLine(i);
                    for (int j = 0; j < width; ++j) {
                        for (int k = 0; k < channels; ++k) {
                            long scaled = std::lround(src[k * (width * height) + i * width + j] * 255.0f);
                            dest[j * channels + k] = clip_uint8(scaled);
                        }
                    }
                }
                return image;
            }
            else {
                // Fallback: use stride-based access (safe for non-contiguous arrays).
                for (int i = 0; i < height; ++i) {
                    unsigned char* dest = image.scanLine(i);
                    for (int j = 0; j < width; ++j) {
                        for (int k = 0; k < channels; ++k) {
                            float val = read_float_at(i, j, k);
                            long scaled = std::lround(val * 255.0f);
                            dest[j * channels + k] = clip_uint8(scaled);
                        }
                    }
                }
                return image;
            }
        }
        else
        {
            throw std::invalid_argument("nparray_to_qimage: Expected dtype=uint8 or float");
        }
    }

//------------------------------------------------------------------------------
// Helper: Convert a QVariant to a py::object with better type handling
    py::object convertQVariantToPyObject(const QVariant& var)
    {
        // If the QVariant wraps a QImage, use our conversion function.
        if (var.canConvert<QImage>()) {
            QImage image = var.value<QImage>();
            return qimage_to_nparray(image);  // Returns a py::array for an image.
        }
        // Handle fundamental types.
        if (var.type() == QMetaType::Int)
            return py::cast(var.toInt());
        if (var.type() == QMetaType::Double)
            return py::cast(var.toDouble());
        if (var.type() == QMetaType::Bool)
            return py::cast(var.toBool());
        if (var.type() == QMetaType::QString)
            return py::cast(var.toString().toStdString());
        if (var.type() == QVariant::PointF) {
            QPointF point = var.toPointF();
            return py::cast(std::complex<double>(point.x(), point.y()));  // Convert to Python complex
        }

        // Handle lists.
        if (var.canConvert<QVariantList>()) {
            QVariantList list = var.toList();
            py::list pyList;
            for (const QVariant& item : qAsConst(list)) {
                pyList.append(convertQVariantToPyObject(item));
            }
            return pyList;
        }
        // Handle maps.
        if (var.canConvert<QVariantMap>()) {
            QVariantMap map = var.toMap();
            py::dict pyDict;
            for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
                pyDict[py::cast(it.key().toStdString())] =
                    convertQVariantToPyObject(it.value());
            }
            return pyDict;
        }
        // Fallback: convert to string.
        return py::cast(var.toString().toStdString());
    }

    // Helper structure for docstring parameter info.
    struct DocParamInfo {
        QString type;
        QString description;
    };

    // Returns a pair where the first element is the common description (first paragraph)
    // and the second element is a map from parameter names to DocParamInfo.
    std::pair<QString, std::map<QString, DocParamInfo>> parseDocstring(const QString& docString) {
        if (docString.isEmpty())
            return {};

        std::map<QString, DocParamInfo> params;

        // Extract the common description (first paragraph)
        QRegularExpression descExp(R"(^([\s\S]*?)\n\n)");
        auto descMatch = descExp.match(docString);
        QString description = descMatch.hasMatch() ? descMatch.captured(1).trimmed() : "";

        // Locate the Args: section
        QRegularExpression argsSectionExp(R"(Args:\s*((?:.|\n)*))");
        auto argsMatch = argsSectionExp.match(docString);
        if (argsMatch.hasMatch()) {
            QString argsSection = argsMatch.captured(1);
            // Capture parameter lines (one parameter per line)
            QRegularExpression paramExp(R"(^\s*(\w+)\s*\(([^,)]+)(?:,\s*optional)?\):\s*(.+)$)",
                QRegularExpression::MultilineOption);
            QRegularExpressionMatchIterator it = paramExp.globalMatch(argsSection);
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                auto name = match.captured(1);
                DocParamInfo param;
                param.type = match.captured(2);
                param.description = match.captured(3);
                params[name] = param;
            }
        }
        return { description, params };
    }

} // anonymous namespace

// Forward declare ScriptModelImpl
class ScriptModelImpl : public QObject
{
public:
    explicit ScriptModelImpl(const QString& venvPath);
    ~ScriptModelImpl();

    // life-cycle
    void initialize(); // must be called from impl thread (we call via invokeMethod)
    void shutdown();   // clean shutdown; will stop interpreter and release resources

    // operations (all run inside impl thread)
    void loadScript(ScriptModel* model, const QString& path);
    std::vector<FunctionInfo> getFunctionInfos(); // copy out function list (safe across threads)
    QVariant callFunction(const QString& callable, const QVariantList& args,
        std::weak_ptr<EffectRunCallback> callback,
        const QVariantMap& kwargs,
        bool isStoppable); // added isStoppable

private:
    // Interior state (moved from previous ScriptModel)
    bool mValid = false;
    std::weak_ptr<EffectRunCallback> mCallback;
    std::vector<FunctionInfo> mFunctionInfos;
    std::atomic_bool mIsShuttingDown = false;
    QString mVenvPath;

    // Python interpreter scope
    struct PythonScope {
        py::scoped_interpreter interpreter;
        py::gil_scoped_release release;
        PythonScope() {}
    };
    std::unique_ptr<PythonScope> mPythonScope;
    int m_console_width_chars = 80; // default width for terminal emulation (tqdm, etc.)
};

// Implementation ----------------------------------------------------------

ScriptModelImpl::ScriptModelImpl(const QString& venvPath)
    : mVenvPath(venvPath.trimmed())
{
    // empty; real interpreter initialization occurs in initialize() inside the impl thread
}

ScriptModelImpl::~ScriptModelImpl()
{
    // ensure shutdown called from owning thread before destruction
}

void ScriptModelImpl::initialize()
{
    // Run inside impl thread
    if (!isPythonInstalled())
    {
        showErrorAsync(QObject::tr("Matching Python is not installed."),
            QObject::tr("Matching Python is not installed: ") + PY_VERSION);
        mValid = false;
        return;
    }

    try {
        // Initialize interpreter (scoped_interpreter must live in impl thread)
        mPythonScope = std::make_unique<PythonScope>();
        mValid = true;

        // If virtualenv specified, manipulate env vars (affects child processes only)
        if (!mVenvPath.isEmpty())
        {
            qputenv("VIRTUAL_ENV", mVenvPath.toUtf8());

#ifdef Q_OS_WIN
            QString scriptsPath = QDir::toNativeSeparators(mVenvPath + "/Scripts");
#else
            QString scriptsPath = QDir::toNativeSeparators(mVenvPath + "/bin");
#endif
            QByteArray currentPath = qgetenv("PATH");
            QByteArray sep(1, QDir::listSeparator().toLatin1());
            qputenv("PATH", (scriptsPath.toUtf8() + sep + currentPath));
            qputenv("PYTHONHOME", QByteArray());
        }
    }
    catch (const std::exception& e) {
        qWarning() << "Error initializing Python in impl thread:" << e.what();
        mValid = false;
    }
}


void ScriptModelImpl::shutdown()
{
    mIsShuttingDown = true;
    // Release python resources inside impl thread
    mPythonScope.reset();
    // clear cached function info
    mFunctionInfos.clear();
}

void ScriptModelImpl::loadScript(ScriptModel* model, const QString& path)
{
    if (!mValid)
        return;

    py::gil_scoped_acquire acquire;

    // insert venv path site-packages if needed
    try {
        py::module_ sys = py::module_::import("sys");

        py::list sysPath = sys.attr("path");
        //qDebug() << "Python sys.path:" << QString::fromStdString(py::str(sysPath).cast<std::string>());
        for (py::handle path_item : sysPath) {
            std::string pathStr = py::str(path_item).cast<std::string>();
            QString qpath = QString::fromStdString(pathStr) + "/site-packages";
            if (QFileInfo::exists(qpath)) {
                // Append path to sys.path if needed.
                if (mVenvPath.isEmpty())
                    sys.attr("path").attr("append")(qpath.toStdString());
#ifdef Q_OS_WIN
                QString root = QFileInfo(QString::fromStdString(pathStr)).dir().path();
                SetDllDirectoryW(reinterpret_cast<LPCWSTR>(root.utf16()));
#endif
            }
        }

        if (!mVenvPath.isEmpty()) {
            sys.attr("path").attr("insert")(0, (mVenvPath + "/Lib/site-packages").toStdString());
        }

        py::module_ mainModule = py::module_::import("__main__");
        py::dict globals = mainModule.attr("__dict__");

        //static PythonFdRedirector* fdRedirector = nullptr;
        //if (!fdRedirector)
        //    fdRedirector = new PythonFdRedirector(this);

        py::class_<PythonQtStream>(mainModule, "QtStream")
            .def(py::init<>())
            .def("write", &PythonQtStream::write)
            .def("flush", &PythonQtStream::flush)
            .def("isatty", &PythonQtStream::isatty)
            .def("fileno", &PythonQtStream::fileno)
            .def_property_readonly("encoding", &PythonQtStream::encoding);

        py::object qtOut = globals["QtStream"]();
        py::object qtErr = globals["QtStream"]();

        sys.attr("stdout") = qtOut;
        sys.attr("stderr") = qtErr;

        // Sink -> Qt widget
        PythonQtStream::sink = [model](const std::string& s) {
            emit model->appendPythonOutput(QString::fromUtf8(s.c_str()));
            };


        // Override terminal size for tqdm and others
        py::module_ shutil = py::module_::import("shutil");
        py::module_ os = py::module_::import("os");

        // Named tuple type
        py::object terminal_size = os.attr("terminal_size");

        // Our implementation
        auto get_size = py::cpp_function(
            [this, terminal_size](py::args args, py::kwargs kwargs) {
                int cols = m_console_width_chars;
                int rows = 24;

                if (cols < 80) cols = 80;

                return terminal_size(py::make_tuple(cols, rows));
            }
        );

        // Override BOTH
        shutil.attr("get_terminal_size") = get_size;
        os.attr("get_terminal_size") = get_size;

        // Also set environment variables
        //py::object os_environ = os.attr("environ");
        //os_environ["COLUMNS"] = py::str("80");
        //os_environ["LINES"] = py::str("24");

        os.attr("isatty") = py::cpp_function([](int fd) {
            return true;
            });

        globals["_send_image"] = py::cpp_function([this](const py::array& image) {
            // send_image must emit in main thread (showing images via callback)
            if (auto cb = mCallback.lock()) {
                if (!cb->isInterrupted()) {
                    QImage img = nparray_to_qimage(image);
                    emit cb->sendImage(img);
                    return true;
                }
            }
            return false;
            });

        globals["_check_interrupt"] = py::cpp_function([this]() { 
            if (mIsShuttingDown)
                return true;
            if (auto obj = mCallback.lock())
            {
                return obj->isInterrupted();
            }
            return true;
        });

        // Load script text from file
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            throw std::runtime_error(("Failed to open script: " + path.toStdString()));
        }
        QTextStream in(&file);
        auto text = in.readAll().toStdString();
        py::eval<py::eval_statements>(text, globals);

        // Inspect functions
        py::module_ inspect = py::module_::import("inspect");
        py::object param_empty = inspect.attr("Parameter").attr("empty");
        mFunctionInfos.clear();

        py::object items = globals.attr("items")();
        for (py::handle item : items) {
            py::tuple pair = item.cast<py::tuple>();
            std::string name = py::str(pair[0]).cast<std::string>();
            py::object obj = pair[1];

            if (name.empty() || name[0] == '_')
                continue;

            if (!inspect.attr("isfunction")(obj).cast<bool>())
                continue;

            if (py::str(obj.attr("__module__")).cast<std::string>() != "__main__")
                continue;

            std::string qualname = py::str(obj.attr("__qualname__")).cast<std::string>();
            if (qualname.find('.') != std::string::npos)
                continue;

            FunctionInfo info;
            info.name = QString::fromStdString(name);

            py::object sig = inspect.attr("signature")(obj);
            info.signature = QString::fromStdString(py::str(sig).cast<std::string>());

            py::object docObj = inspect.attr("getdoc")(obj);
            std::string docStr = docObj.is_none() ? std::string() : py::str(docObj).cast<std::string>();
            info.doc = QString::fromStdString(docStr);

            auto docInfo = parseDocstring(info.doc);
            info.fullName = docInfo.first.isEmpty() ? info.name : docInfo.first;

            py::object params_items = sig.attr("parameters").attr("items")();
            for (py::handle paramItem : params_items) {
                py::tuple p = paramItem.cast<py::tuple>();
                std::string paramName = py::str(p[0]).cast<std::string>();
                py::object paramObj = p[1];

                ParameterInfo param;
                param.name = QString::fromStdString(paramName);
                param.fullName = param.name;
                param.kind = QString::fromStdString(py::str(paramObj.attr("kind")).cast<std::string>());

                py::object def = paramObj.attr("default");
                if (!def.is_none() && def.ptr() != param_empty.ptr()) {
                    param.defaultValue = QString::fromStdString(py::str(def).cast<std::string>());
                }

                py::object ann = paramObj.attr("annotation");
                if (ann.ptr() != param_empty.ptr()) {
                    param.annotation = QString::fromStdString(py::str(ann).cast<std::string>());
                }

                QString qParamName = QString::fromStdString(paramName);
                auto it = docInfo.second.find(qParamName);
                if (it != docInfo.second.end() && !it->second.description.isEmpty()) {
                    param.fullName = it->second.description;
                    param.description = it->second.description;
                }

                info.parameters.push_back(std::move(param));
            }

            mFunctionInfos.push_back(std::move(info));
        }
    }
    catch (const py::error_already_set& e) {
        qWarning() << "Python error while loading script in impl thread:" << e.what();
        showErrorAsync(QObject::tr("Script Execution Error"),
            QObject::tr("Error executing script: ") + QString::fromUtf8(e.what()));
    }
    catch (const std::exception& e) {
        qWarning() << "Error loading script in impl thread:" << e.what();
        showErrorAsync(QObject::tr("Script Execution Error"),
            QObject::tr("Error executing script: ") + e.what());
    }
}

std::vector<FunctionInfo> ScriptModelImpl::getFunctionInfos()
{
    return mFunctionInfos; // copy
}

QVariant ScriptModelImpl::callFunction(const QString& callable, const QVariantList& args,
                                       std::weak_ptr<EffectRunCallback> callback,
                                       const QVariantMap& kwargs,
                                       bool isStoppable)
{
    if (!mValid)
        return {};

    if (auto obj = mCallback.lock())
    {
        if (obj->isInterrupted())
            return {};
    }

    py::gil_scoped_acquire acquire;

    py::module_ mainModule = py::module_::import("__main__");
    py::dict globals = mainModule.attr("__dict__");

    std::string funcName = callable.toStdString();
    py::object pyFunc = globals.attr("get")(funcName.c_str(), py::none());
    if (pyFunc.is_none()) {
        qWarning() << "Function" << callable << "not found (impl thread).";
        return QVariant();
    }

    py::list pyArgs(args.size());
    for (int i = 0; i < args.size(); ++i) {
        pyArgs[i] = convertQVariantToPyObject(args[i]);
    }

    py::dict pyKwargs;
    for (auto it = kwargs.constBegin(); it != kwargs.constEnd(); ++it) {
        pyKwargs[py::cast(it.key().toUtf8().constData())] = convertQVariantToPyObject(it.value());
    }

    py::object result;
    try {
        mCallback = callback;
        result = pyFunc(*pyArgs, **pyKwargs);
    }
    catch (const py::error_already_set& e) {
        qWarning() << "Python exception calling function" << callable << ":" << e.what();
        auto ptr = callback.lock();
        // Use the isStoppable that was captured at call-time to keep original semantics
        if (!isStoppable || (ptr && !ptr->isInterrupted())) {
            showErrorAsync(QObject::tr("Python Call Error"),
                           QObject::tr("Python exception calling ") + callable + ": " + QString::fromUtf8(e.what()));
        }
        return QVariant();
    }
    catch (const std::exception& e) {
        qWarning() << "Error calling function" << callable << ":" << e.what();
        auto ptr = callback.lock();
        if (!isStoppable || (ptr && !ptr->isInterrupted())) {
            showErrorAsync(QObject::tr("Python Call Error"),
                           QObject::tr("Error calling function ") + callable + ": " + e.what());
        }
        return QVariant();
    }

    if (py::isinstance<py::array>(result)) {
        try {
            py::array arr = result.cast<py::array>();
            QImage img = nparray_to_qimage(arr);
            return QVariant::fromValue(img);
        }
        catch (const std::exception& ex) {
            qWarning() << "Failed to convert numpy array to QImage (impl):" << ex.what();
        }
    }

    std::string resStr = py::str(result);
    return QVariant(QString::fromUtf8(resStr.c_str()));
}

// -----------------------------
// ScriptModel (front-end) that forwards to impl via blocking queued calls
// -----------------------------

ScriptModel::ScriptModel(QWidget* parent, const QString& venvPath)
    : QObject(parent)
{
    // create impl and its thread
    mImplThread = new QThread();
    // create impl in current thread then move to impl thread
    mImpl = std::make_unique<ScriptModelImpl>(venvPath);

    // move to worker thread
    mImpl->moveToThread(mImplThread);
    mImplThread->start();

    // initialize inside impl thread (blocking)
    bool ok = QMetaObject::invokeMethod(mImpl.get(),
        [this]() { mImpl->initialize(); },
        Qt::BlockingQueuedConnection);
    if (!ok) {
        qWarning() << "Failed to initialize ScriptModelImpl in worker thread.";
    }
}

ScriptModel::~ScriptModel()
{
    // Ask impl to shutdown (blocking)
    if (mImpl) {
        QMetaObject::invokeMethod(mImpl.get(),
            [this]() { mImpl->shutdown(); },
            Qt::BlockingQueuedConnection);
    }

    if (mImplThread) {
        mImplThread->quit();
        mImplThread->wait();
        delete mImplThread;
        mImplThread = nullptr;
    }
    mImpl.reset();
}

void ScriptModel::LoadScript(const QString& path)
{
    if (!mImpl) return;
    // forward to impl (blocking)
    QMetaObject::invokeMethod(mImpl.get(),
        [this, path]() { mImpl->loadScript(this, path); },
        Qt::BlockingQueuedConnection);
}

void ScriptModel::setupActions(QMenu* fileMenu, QMenu* effectsMenu, QMap<int, QAction*>& effectsActMap)
{
    if (!mImpl)
        return;

    std::vector<FunctionInfo> infos = mImpl->getFunctionInfos();
    if (infos.empty())
        return;

    const auto parent = this->parent();

    bool fileSepAdded = false;
    bool effectsSepAdded = false;

    QAction* separator = nullptr;

    for (const auto& funcInfo : infos) {
        QAction* effectAction = new QAction(funcInfo.fullName, parent);
        if (funcInfo.isCreatingFunction())
        {
            // Add separator before creating-function block only once and only if there are existing items.
            if (!fileSepAdded) {
                if (!fileMenu->actions().isEmpty()) {
                    const auto firstAction = fileMenu->actions().constFirst();
                    separator = fileMenu->insertSeparator(firstAction);
                }
                fileSepAdded = true;
            }

            auto effect = funcInfo.parameters.empty()
                ? static_cast<AbstractEffect*>(new ScriptEffect(this, funcInfo))
                : new ScriptEffectWithSettings(this, funcInfo);

            QObject::connect(effectAction, &QAction::triggered, [effect] { effect->applyEffect(nullptr); });
            fileMenu->insertAction(separator, effectAction);  // Insert at the start
        }
        else
        {
            // Add separator before appending to effects menu only once and only if menu already has items.
            if (!effectsSepAdded) {
                if (!effectsMenu->actions().isEmpty()) {
                    effectsMenu->addSeparator();
                }
                effectsSepAdded = true;
            }

            // Use DataSingleton to register each function.
            const int type = DataSingleton::Instance()->addScriptActionHandler(this, funcInfo);
            QObject::connect(effectAction, SIGNAL(triggered()), parent, SLOT(effectsAct()));
            effectsMenu->addAction(effectAction);
            effectsActMap.insert(type, effectAction);
        }
    }
}

QVariant ScriptModel::call(const QString& callable,
    const QVariantList& args,
    std::weak_ptr<EffectRunCallback> callback,
    const QVariantMap& kwargs)
{
    // Preserve original behavior: compute this immediately
    const bool isStoppable = !callback.expired();

    if (!mImpl) return {};

    qDebug() << "Entering ScriptModel::call.";

    QVariant result;
    bool ok = QMetaObject::invokeMethod(mImpl.get(),
        [&result, callable, args, callback, kwargs, isStoppable, this]() {
            result = mImpl->callFunction(callable, args, callback, kwargs, isStoppable);
        },
        Qt::BlockingQueuedConnection);

    if (!ok) {
        qWarning() << "Failed to call function in impl thread.";
        return QVariant();
    }
    qDebug() << "Leaving ScriptModel::call.";
    return result;
}

// Simple validator (run in impl thread)
int ScriptModel::ValidatePythonSystem()
{
    try {
        py::scoped_interpreter interpreter;
        py::gil_scoped_release release_gil;
        // Acquire the GIL so that Python calls are thread-safe.
        py::gil_scoped_acquire acquire;
        py::module_ sys = py::module_::import("sys");
        py::object sysPathObj = sys.attr("path");
        if (!py::isinstance<py::list>(sysPathObj)) {
            qWarning() << "Python system invalid: sys.path is not a list.";
            return 1;
        }
        py::list sysPath = sysPathObj.cast<py::list>();
        if (sysPath.size() == 0) {
            qWarning() << "Python system invalid: sys.path is empty.";
            return 2;
        }
        py::module_::import("os");
    }
    catch (const std::exception& e) {
        qWarning() << "Python system validation failed:" << e.what();
        return 3;
    }
    return 0;
}
