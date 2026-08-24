#include "PyInterpreter.hpp"

#include "nodegraph/NodeGraph.hpp"

#include <pybind11/embed.h>

namespace py = pybind11;

static const char* CAPTURE_SETUP = R"PY(
import sys
from io import StringIO
__parametal_stdout = StringIO()
__parametal_stderr = StringIO()
__parametal_old_stdout = sys.stdout
__parametal_old_stderr = sys.stderr
sys.stdout = __parametal_stdout
sys.stderr = __parametal_stderr
)PY";

static const char* CAPTURE_FLUSH = R"PY(
__parametal_stdout_value = __parametal_stdout.getvalue()
__parametal_stderr_value = __parametal_stderr.getvalue()
__parametal_stdout.truncate(0)
__parametal_stdout.seek(0)
__parametal_stderr.truncate(0)
__parametal_stderr.seek(0)
)PY";

struct PyCtx {
    std::unique_ptr<py::scoped_interpreter> interpreter;
    PyObject* interactive_interp = nullptr;
};

static PyCtx* getCtx(void* impl) {
    return static_cast<PyCtx*>(impl);
}

PyInterpreter::PyInterpreter()
    : impl(new PyCtx()) {}

PyInterpreter::~PyInterpreter() {
    shutdown();
    delete getCtx(impl);
}

bool PyInterpreter::initialize(NodeGraph& graph) {
    PyCtx* ctx = getCtx(impl);
    if (ctx->interpreter) {
        return true;
    }
    try {
        ctx->interpreter = std::make_unique<py::scoped_interpreter>();

        PyRun_SimpleString(CAPTURE_SETUP);
        PyRun_SimpleString("import code, __main__; __parametal_interp = code.InteractiveInterpreter(__main__.__dict__)");

        PyObject* main = PyImport_AddModule("__main__");
        if (main) {
            ctx->interactive_interp = PyObject_GetAttrString(main, "__parametal_interp");
        }

        py::module_ module = py::module_::import("parametal");
        module.attr("_graph") = py::cast(&graph, py::return_value_policy::reference);
        PyRun_SimpleString("import parametal as pm");
        PyRun_SimpleString(R"PY(
def default_graph():
    pm.default_graph()

def api():
    print("""
ParaMetal Python API
======================

Module:  import parametal as pm
Graph:    g = pm.get_graph()
Sample:   default_graph()

Graph Methods:
  g.add_node(type, name='', x=0, y=0)  -> Node
  g.remove_node(node_id)
  g.get_node(name)                    -> Node
  g.nodes                             -> list[Node]
  g.connect(output_socket, input_socket)
  g.disconnect(edge_id)
  g.registry                          -> Registry

Node Properties:
  node.name
  node.type

Node Methods:
  node.get(param_name)                -> value
  node.set(param_name, value)
  node.input(name)                    -> Socket
  node.output(name)                   -> Socket
  node.inputs                         -> list[Socket]
  node.outputs                        -> list[Socket]

Socket Properties:
  socket.name
  socket.type

Examples:
  g = pm.get_graph()
  a = g.add_node('model', 'My Model', 0, 0)
  b = g.add_node('transform', 'My Transform', 200, 0)
  g.connect(a.output('Mesh'), b.input('Mesh'))

  for e in g.edges:
      print(e)

  e = g.edges[0]
  g.disconnect(e)
""")

def registry():
    for t in pm.get_graph().registry.all_node_types:
        print(f"  {t.id}  ({t.display_name})")
)PY");

        return true;
    } catch (const py::error_already_set&) {
        return false;
    }
}

void PyInterpreter::shutdown() {
    if (!impl) return;
    PyCtx* ctx = getCtx(impl);
    if (ctx->interpreter) {
        try {
            py::module_::import("parametal").attr("_graph") = py::none();
        } catch (const py::error_already_set&) {
            PyErr_Clear();
        }
    }
    if (ctx->interactive_interp) {
        Py_DECREF(ctx->interactive_interp);
        ctx->interactive_interp = nullptr;
    }
    ctx->interpreter.reset();
}

bool PyInterpreter::isInitialized() const {
    PyCtx* ctx = getCtx(impl);
    return ctx && ctx->interpreter != nullptr;
}

bool PyInterpreter::runSource(const std::string& source) {
    PyCtx* ctx = getCtx(impl);
    if (!ctx || !ctx->interpreter || !ctx->interactive_interp) {
        return false;
    }

    PyObject* result = PyObject_CallMethod(
        ctx->interactive_interp, "runsource", "s", source.c_str());

    bool incomplete = false;
    if (result) {
        incomplete = (result == Py_True);
        Py_DECREF(result);
    } else {
        PyErr_Clear();
    }

    // Flush capture buffers
    PyRun_SimpleString(CAPTURE_FLUSH);
    return incomplete;
}

static std::string readPythonVariable(const char* name) {
    PyObject* main = PyImport_AddModule("__main__");
    if (!main) return "";
    PyObject* dict = PyModule_GetDict(main);
    if (!dict) return "";
    PyObject* val = PyDict_GetItemString(dict, name);
    if (!val || !PyUnicode_Check(val)) return "";
    Py_ssize_t len = 0;
    const char* str = PyUnicode_AsUTF8AndSize(val, &len);
    if (!str) return "";
    return std::string(str, static_cast<size_t>(len));
}

std::string PyInterpreter::consumeOutput() {
    return readPythonVariable("__parametal_stdout_value");
}

std::string PyInterpreter::consumeError() {
    return readPythonVariable("__parametal_stderr_value");
}

std::string PyInterpreter::pythonVersion() const {
    const char* ver = Py_GetVersion();
    if (!ver) return "";

    const char* end = ver;
    while (*end && *end != ' ') ++end;

    return std::string(ver, static_cast<size_t>(end - ver));
}
