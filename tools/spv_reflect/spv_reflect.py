import sys
import json
import subprocess
import os

if len(sys.argv) < 6:
    print("Usage: python3 spv_reflect.py <spirv_cross_path> <vertex.spv> <pixel.spv> <pipeline_name> <output_base_path>")
    sys.exit(1)

spirv_cross_bin = sys.argv[1]
vertex_spv = sys.argv[2]
pixel_spv = sys.argv[3]
pipeline_name = sys.argv[4]
out_base = sys.argv[5]

def get_spirv_reflection(spv_path):
    try:
        result = subprocess.run([spirv_cross_bin, spv_path, "--reflect"], capture_output=True, text=True, check=True)
        return json.loads(result.stdout)
    except subprocess.CalledProcessError as e:
        print(f"Failed to run spirv-cross on {spv_path}: {e}")
        print(f"Stderr: {e.stderr}")
        sys.exit(1)

vs_refl = get_spirv_reflection(vertex_spv)

stride = 0
attributes = []

inputs = sorted(vs_refl.get("inputs", []), key=lambda x: x.get("location", 0))

for inp in inputs:
    name = inp.get("name", "").lower()
    typ = inp.get("type", "")
    
    semantic = "TEXCOORD"
    if "pos" in name:
        semantic = "POSITION"
    elif "col" in name:
        semantic = "COLOR"
    elif "norm" in name:
        semantic = "NORMAL"
    elif "uv" in name or "tex" in name:
        semantic = "TEXCOORD"

    attributes.append({
        "semantic": semantic,
        "offset": stride
    })

    if typ == "vec4":
        stride += 16
    elif typ == "vec3":
        stride += 12
    elif typ == "vec2":
        stride += 8
    elif typ == "float":
        stride += 4
    else:
        stride += 16

out_data = {
    "name": pipeline_name,
    "stride": stride,
    "attributes": attributes,
    "bindings": []
}

os.makedirs(os.path.dirname(out_base), exist_ok=True)
with open(out_base + ".json", "w") as f:
    json.dump(out_data, f, indent=2)

with open(out_base + ".h", "w") as f:
    f.write(f"// Auto-generated dummy header for {pipeline_name} (Not used by engine)\n")

print(f"Generated reflection for {pipeline_name}")
