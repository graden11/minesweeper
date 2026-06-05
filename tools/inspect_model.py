import ctypes, sys, os

# Load ONNX Runtime
lib = ctypes.CDLL("/mnt/d/jetbrains/clion-project/httpserver/third_party/onnxruntime/lib/libonnxruntime.so")

# Define the OrtApi struct (we need the function table)
class OrtApiBase(ctypes.Structure):
    _fields_ = [("GetApi", ctypes.c_void_p)]

# Simpler approach — use ort C API directly
def ort(name, *args):
    func = getattr(lib, f"Ort{name}")
    return func(*args)

env = ctypes.c_void_p()
ort("CreateEnv", 3, 0, ctypes.byref(env))

opts = ctypes.c_void_p()
ort("CreateSessionOptions", ctypes.byref(opts))

sess = ctypes.c_void_p()
model = b"/mnt/d/jetbrains/clion-project/httpserver/models/yolov8l.onnx"
st = ort("CreateSession", env, model, opts, ctypes.byref(sess))
print(f"CreateSession -> {st}")

if st != 0:
    print("FAILED")
    sys.exit(1)

n_inputs = ctypes.c_size_t()
ort("SessionGetInputCount", sess, ctypes.byref(n_inputs))
print(f"Inputs: {n_inputs.value}")

# Get input name + typeinfo
for i in range(n_inputs.value):
    name_ptr = ctypes.c_char_p()
    ort("SessionGetInputName", sess, i, ctypes.byref(name_ptr))
    name = name_ptr.value.decode() if name_ptr else "?"

    typeinfo = ctypes.c_void_p()
    ort("SessionGetInputTypeInfo", sess, i, ctypes.byref(typeinfo))

    tinfo = ctypes.c_void_p()
    ort("CastTypeInfoToTensorInfo", typeinfo, ctypes.byref(tinfo))

    ndims = ctypes.c_size_t()
    ort("GetDimensionsCount", tinfo, ctypes.byref(ndims))

    dims = (ctypes.c_int64 * ndims.value)()
    ort("GetDimensions", tinfo, dims, ndims)

    shapes = [dim for dim in dims]
    print(f"  {name}: shape={shapes}")

n_outputs = ctypes.c_size_t()
ort("SessionGetOutputCount", sess, ctypes.byref(n_outputs))
print(f"Outputs: {n_outputs.value}")

for i in range(n_outputs.value):
    name_ptr = ctypes.c_char_p()
    ort("SessionGetOutputName", sess, i, ctypes.byref(name_ptr))
    name = name_ptr.value.decode() if name_ptr else "?"

    typeinfo = ctypes.c_void_p()
    ort("SessionGetOutputTypeInfo", sess, i, ctypes.byref(typeinfo))
    tinfo = ctypes.c_void_p()
    ort("CastTypeInfoToTensorInfo", typeinfo, ctypes.byref(tinfo))

    ndims = ctypes.c_size_t()
    ort("GetDimensionsCount", tinfo, ctypes.byref(ndims))
    dims = (ctypes.c_int64 * ndims.value)()
    ort("GetDimensions", tinfo, dims, ndims)

    shapes = [dim for dim in dims]
    print(f"  {name}: shape={shapes}")
