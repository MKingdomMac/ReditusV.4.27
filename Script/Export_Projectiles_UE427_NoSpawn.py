"""Reditus projectile exporter v2: class defaults and stored component templates.
UE4.27.2 editor Python. No actor spawning or construction-script execution.
No native table exporter, Blueprint edits, imports, or asset/level saves.
Unknown values stay blank. Read README.md before running.
"""
import csv
import datetime
import hashlib
import json
import math
import os
import re
import traceback
import unreal

EXPORTER_VERSION = 2
BASE_BLUEPRINT = "/Game/TurnBasedJRPGTemplate/Blueprints/Projectiles/BP_ProjectileBase"


PROJECTILE_FOLDERS = ["/Game/TurnBasedJRPGTemplate/Blueprints/Projectiles"]


HEADER_TEMPLATE_CSV = ""


OUTPUT_DIRECTORY = ""


COLUMNS = [
    "Projectile Class", "Speed", "FlightEffect", "FlightEffectNiagara",
    "HitEffectParticle", "HitEffectParticleNiagara", "Hit Sound", "FaceTarget",
    "Visual Scale", "VisualRotationOffset", "HitEffectScale",
    "DestroyDelayAfterHit", "UsePointLight", "LightColor", "LightIntensity",
    "LightAttentuationRadius",
]


ALIASES = {
    "Projectile Class": ["ProjectileClass"],
    "Speed": ["speed", "Speed"],
    "FlightEffect": ["FlightEffect", "FlightEffectParticle"],
    "FlightEffectNiagara": ["FlightEffectNiagara", "FlightEffectParticleNiagara"],
    "HitEffectParticle": ["hitEffect", "HitEffectParticle"],
    "HitEffectParticleNiagara": ["HitEffectParticleNiagara", "HitEffectNiagara"],
    "Hit Sound": ["hitSound", "HitSound", "Hit Sound"],
    "FaceTarget": ["FaceTarget"],
    "Visual Scale": ["VisualScale", "Visual Scale"],
    "VisualRotationOffset": ["VisualRotationOffset"],
    "HitEffectScale": ["HitEffectScale"],
    "DestroyDelayAfterHit": ["DestroyDelayAfterHit"],
    "UsePointLight": ["UsePointLight"],
    "LightColor": ["LightColor"],
    "LightIntensity": ["LightIntensity"],
    "LightAttentuationRadius": ["LightAttenuationRadius", "LightAttentuationRadius"],
}


ASSET_COLUMNS = {
    "FlightEffect", "FlightEffectNiagara", "HitEffectParticle",
    "HitEffectParticleNiagara", "Hit Sound",
}


COMPONENT_PROPERTIES = [
    "relative_location", "relative_rotation", "relative_scale3d",
    "absolute_location", "absolute_rotation", "absolute_scale", "visible",
    "hidden_in_game", "auto_activate", "is_editor_only", "mobility",
    "component_tags", "template", "override_materials", "static_mesh",
    "skeletal_mesh", "sound", "volume_multiplier", "pitch_multiplier",
    "intensity", "intensity_units", "light_color", "attenuation_radius",
    "cast_shadows", "use_temperature", "temperature", "affects_world",
    "use_inverse_squared_falloff", "light_falloff_exponent", "source_radius",
    "rotation_rate", "pivot_translation", "rotation_in_local_space",
    "initial_speed", "max_speed", "projectile_gravity_scale",
    "rotation_follows_velocity", "is_homing_projectile", "homing_acceleration_magnitude",
    "should_bounce", "bounciness", "sphere_radius", "capsule_radius",
    "capsule_half_height", "box_extent", "collision_enabled",
    "generate_overlap_events", "child_actor_class", "user_parameters",
    "instance_parameters", "override_parameters", "parameter_overrides",
]


def normalized(value):
    """Match friendly names and UE user-struct generated names, not substrings."""
    value = re.sub(r"_\d+_[0-9A-Fa-f]{32}$", "", str(value))
    value = re.sub(r"[^a-z0-9]", "", value.lower())
    return value.replace("attentuation", "attenuation")


def object_path(value):
    if value is None:
        return ""
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def tagged_path(text):
    text = str(text or "").strip()
    if "'" in text:
        text = text.split("'", 1)[1].rsplit("'", 1)[0]
    return text.strip('"')


def asset_text(value):
    path = object_path(value)
    if not path or path == "None":
        return "None"
    # Raw object paths are accepted by object/soft-object property import and
    # avoid hardcoding SoundWave vs SoundCue or a Niagara plugin class name.
    return path


def number(value):
    result = float(value)
    if not math.isfinite(result):
        raise ValueError("non-finite number")
    return format(result, ".9g")


def vector_text(value):
    return "(X={},Y={},Z={})".format(number(value.x), number(value.y), number(value.z))


def rotator_text(value):
    return "(Pitch={},Yaw={},Roll={})".format(
        number(value.pitch), number(value.yaw), number(value.roll))


def color_text(value):
    return "(R={},G={},B={},A={})".format(
        number(value.r), number(value.g), number(value.b), number(value.a))


def cell_value(column, value):
    if column in ASSET_COLUMNS or column == "Projectile Class":
        return asset_text(value)
    if column in ("FaceTarget", "UsePointLight"):
        return "True" if bool(value) else "False"
    if column in ("Visual Scale", "HitEffectScale"):
        return vector_text(value)
    if column == "VisualRotationOffset":
        return rotator_text(value)
    if column == "LightColor":
        return color_text(value)
    return number(value)


def json_value(value, depth=0):
    if value is None or isinstance(value, (str, bool, int)):
        return value
    if isinstance(value, float):
        return value if math.isfinite(value) else str(value)
    if depth > 4:
        return str(value)
    if all(hasattr(value, attr) for attr in ("pitch", "yaw", "roll")):
        return {k: float(getattr(value, k)) for k in ("pitch", "yaw", "roll")}
    if all(hasattr(value, attr) for attr in ("x", "y", "z")):
        return {k: float(getattr(value, k)) for k in ("x", "y", "z")}
    if all(hasattr(value, attr) for attr in ("r", "g", "b", "a")):
        return {k: float(getattr(value, k)) for k in ("r", "g", "b", "a")}
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    try:
        if hasattr(value, "items"):
            return {str(k): json_value(v, depth + 1) for k, v in value.items()}
        if not isinstance(value, bytes) and hasattr(value, "__iter__"):
            return [json_value(v, depth + 1) for v in value]
    except Exception:
        pass
    return str(value)


def read_property(obj, names):
    for name in names:
        try:
            return True, obj.get_editor_property(name), name
        except Exception:
            pass
    return False, None, None


def unique_row_names(paths):
    groups = {}
    for path in paths:
        name = path.rsplit(".", 1)[-1]
        groups.setdefault(name.casefold(), []).append(path)
    names = {}
    used = set()
    for path in sorted(paths):
        short = path.rsplit(".", 1)[-1]
        name = short
        if len(groups[short.casefold()]) > 1:
            name += "_" + hashlib.sha256(path.encode("utf-8")).hexdigest()[:10]
        while name.casefold() in used:
            name += "_" + hashlib.sha256(path.encode("utf-8")).hexdigest()[:16]
        used.add(name.casefold())
        names[path] = name
    return names


def read_header(path):
    with open(path, "r", encoding="utf-8-sig", newline="") as handle:
        header = next(csv.reader(handle), [])
    if len(header) < 2:
        raise ValueError("CSV template has no DataTable header: " + path)
    return header


def map_headers(header):
    mapping = {}
    unknown = []
    for field in header[1:]:
        matches = [col for col in COLUMNS if normalized(field) == normalized(col)]
        if len(matches) == 1:
            if matches[0] in mapping.values():
                raise RuntimeError("Duplicate table column: " + field)
            mapping[field] = matches[0]
        else:
            unknown.append(field)
    return mapping, unknown


def write_csv(path, header, rows):
    with open(path, "w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=header, extrasaction="raise")
        writer.writeheader()
        writer.writerows(rows)


def write_outputs(output, header, mapping, unknown_columns, header_source, records, errors):
    import_rows = []
    review_rows = []
    component_rows = []
    for record in records:
        if record["compatible_with_BP_ProjectileBase"]:
            row = {header[0]: record["row_name"]}
            row.update({field: record["csv_values"].get(mapping.get(field), "")
                        for field in header[1:]})
            import_rows.append(row)
        review_rows.append({
            "RowName": record["row_name"], "Blueprint": record["blueprint"],
            "GeneratedClass": record["generated_class"],
            "CompatibleWithProjectileBase": record["compatible_with_BP_ProjectileBase"],
            "ComponentsInspected": record.get("components_inspected", False),
            "ReviewRequired": True,
            "UnknownFields": "; ".join(col for col in COLUMNS
                                        if record["field_sources"][col].startswith("UNKNOWN")),
            "Warnings": " | ".join(record["warnings"]),
        })
        for comp in record["components"]:
            props = comp["properties"]
            component_rows.append({
                "RowName": record["row_name"], "Blueprint": record["blueprint"],
                "Component": comp["name"], "Class": comp["class"],
                "Parent": comp.get("parent", ""), "Socket": comp.get("socket", ""),
                "ParticleSystem": props.get("template", ""),
                "NiagaraSystem": comp.get("niagara_system", ""),
                "RelativeLocation": json.dumps(props.get("relative_location")),
                "RelativeRotation": json.dumps(props.get("relative_rotation")),
                "RelativeScale": json.dumps(props.get("relative_scale3d")),
                "PropertiesJSON": json.dumps(comp, ensure_ascii=False, sort_keys=True),
            })
    write_csv(os.path.join(output, "DT_Projectiles_REVIEW.csv"), header, import_rows)
    write_csv(os.path.join(output, "Projectile_Review.csv"), [
        "RowName", "Blueprint", "GeneratedClass", "CompatibleWithProjectileBase",
        "ComponentsInspected", "ReviewRequired", "UnknownFields", "Warnings"], review_rows)
    write_csv(os.path.join(output, "Projectile_Components.csv"), [
        "RowName", "Blueprint", "Component", "Class", "Parent", "Socket",
        "ParticleSystem", "NiagaraSystem", "RelativeLocation", "RelativeRotation",
        "RelativeScale", "PropertiesJSON"], component_rows)
    inventory = {
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "base_blueprint": BASE_BLUEPRINT, "header_source": header_source,
        "unmapped_table_columns": unknown_columns, "projectiles": records, "errors": errors,
        "summary": {"inventoried": len(records), "table_rows": len(import_rows),
                    "components": len(component_rows), "errors": len(errors)},
        "limits": ["Not a behavior converter; no Blueprint graph execution/extraction.",
                   "CSV has unresolved blank fields and requires review before import.",
                   "No existing DataTable rows were merged, changed or imported.",
                   "v2: no actors are instantiated; stored templates may be incomplete.",
                   "Class defaults may differ from runtime spawn-pin overrides.",
                   "Only allowlisted properties are read; this is not a binary asset backup."],
    }
    with open(os.path.join(output, "Projectile_Inventory.json"), "w", encoding="utf-8") as handle:
        json.dump(inventory, handle, indent=2, ensure_ascii=False, allow_nan=False)
    return inventory["summary"]

def read_allowed_properties(obj, names):
    result = {}
    for name in sorted(set(names)):
        found, value, unused = read_property(obj, [name])
        if found:
            result[name] = json_value(value)
    return result


def metadata_candidates(registry):
    """Read registry tags without loading every Blueprint in /Game."""
    metadata = {}
    for asset in list(registry.get_assets_by_class("Blueprint", True) or []):
        path = str(asset.object_path)
        if not path.startswith("/Game/"):
            continue
        generated = tagged_path(asset.get_tag_value("GeneratedClass")) or path + "_C"
        metadata[generated] = {
            "blueprint": path, "generated_class": generated,
            "parent_class": tagged_path(asset.get_tag_value("ParentClass")),
        }
    base_path = BASE_BLUEPRINT + "." + BASE_BLUEPRINT.rsplit("/", 1)[-1] + "_C"
    descendants = {base_path}
    changed = True
    while changed:
        changed = False
        for generated, record in metadata.items():
            if record["parent_class"] in descendants and generated not in descendants:
                descendants.add(generated)
                changed = True
    result = []
    for generated, record in metadata.items():
        path = record["blueprint"]
        if generated == base_path:
            continue
        if generated in descendants or any(path.startswith(f.rstrip("/") + "/")
                                           for f in PROJECTILE_FOLDERS):
            item = dict(record)
            item["compatible"] = generated in descendants
            result.append(item)
    return sorted(result, key=lambda item: item["blueprint"]), metadata


def source_chain(record, metadata):
    chain, seen = [], set()
    current = record
    while current and current["generated_class"] not in seen:
        chain.append(current)
        seen.add(current["generated_class"])
        current = metadata.get(current["parent_class"])
    return chain


def template_owner(component_path, chain):
    """Only class/Blueprint/CDO-owned templates; exclude preview/world actors."""
    for record in chain:
        generated, bp = record["generated_class"], record["blueprint"]
        package = bp.rsplit(".", 1)[0]
        default_prefix = package + ".Default__" + generated.rsplit(".", 1)[-1] + ":"
        if any(component_path.startswith(prefix) for prefix in
               (generated + ":", bp + ":", default_prefix)):
            return generated
    return None


def stored_templates(chain):
    result = []
    # ObjectIterator walks already loaded objects. Only inspect ActorComponents
    # whose full paths prove ownership by one of these source classes/assets.
    for obj in unreal.ObjectIterator():
        if not isinstance(obj, unreal.ActorComponent):
            continue
        path = obj.get_path_name()
        owner = template_owner(path, chain)
        if owner:
            result.append((obj, owner))
    return sorted(result, key=lambda pair: pair[0].get_path_name())


def template_record(comp, owner):
    item = {
        "name": comp.get_name(), "class": comp.get_class().get_path_name(),
        "template_path": comp.get_path_name(), "source_class": owner,
        "properties": read_allowed_properties(comp, COMPONENT_PROPERTIES),
        "parent": "UNKNOWN: SCS template attachment is not resolved",
        "socket": "UNKNOWN",
    }
    niagara_type = getattr(unreal, "NiagaraComponent", None)
    if niagara_type and isinstance(comp, niagara_type):
        item["niagara_system"] = object_path(comp.get_asset())
    return item


def infer_template_fields(data, templates):
    values, sources, warnings = data["csv_values"], data["field_sources"], data["warnings"]
    cascade, niagara, lights = [], [], []
    niagara_type = getattr(unreal, "NiagaraComponent", None)
    for comp, owner in templates:
        if isinstance(comp, unreal.ParticleSystemComponent):
            asset = read_property(comp, ["template"])[1]
            if asset:
                cascade.append((comp, asset))
        if niagara_type and isinstance(comp, niagara_type):
            asset = comp.get_asset()
            if asset:
                niagara.append((comp, asset))
        if isinstance(comp, unreal.PointLightComponent):
            lights.append(comp)
        if isinstance(comp, unreal.MovementComponent):
            warnings.append("Movement template requires legacy/graph review: " + comp.get_name())
        if isinstance(comp, unreal.MeshComponent):
            warnings.append("Mesh template is not represented by current row fields: " + comp.get_name())

    def put(column, value, source):
        if values.get(column, "") in ("", "None"):
            values[column] = cell_value(column, value)
            sources[column] = source

    for column, effects in (("FlightEffect", cascade), ("FlightEffectNiagara", niagara)):
        if len(effects) == 1:
            comp, asset = effects[0]
            put(column, asset, "single stored template (verify flight role): " + comp.get_path_name())
        elif len(effects) > 1:
            warnings.append(column + ": multiple stored templates, including possible inherited "
                            "overrides; not flattened. See full template paths in inventory.")
    if len(lights) == 1:
        light = lights[0]
        for column, prop in (("LightIntensity", "intensity"),
                             ("LightAttentuationRadius", "attenuation_radius")):
            found, value, unused = read_property(light, [prop])
            if found:
                put(column, value, "stored light template: " + prop)
        visibility = [read_property(light, [p])
                      for p in ("visible", "affects_world", "hidden_in_game")]
        if all(p[0] for p in visibility):
            put("UsePointLight", bool(visibility[0][1] and visibility[1][1] and not visibility[2][1]),
                "stored light visibility flags; runtime activation not evaluated")
        try:
            put("LightColor", light.get_light_color(), "stored light GetLightColor (linear)")
        except Exception as exc:
            warnings.append("Light color read failed: " + str(exc))
        warnings.append("Light units, shadows, temperature and offsets need review; "
                        "the current row does not represent all these settings.")
    elif len(lights) > 1:
        warnings.append("Multiple light templates; one-light fields not inferred.")
    if templates:
        warnings.append("Stored templates are authoring data, not a constructed actor. "
                        "Attachment resolution and inherited overrides need review; transforms "
                        "remain in component inventory rather than being guessed into row fields.")


def empty_record(record, row_name):
    return {
        "row_name": row_name, "blueprint": record["blueprint"],
        "generated_class": record["generated_class"],
        "compatible_with_BP_ProjectileBase": record["compatible"],
        "actor_defaults": {}, "components": [], "components_inspected": False,
        "csv_values": {column: "" for column in COLUMNS},
        "field_sources": {column: "UNKNOWN: not recovered" for column in COLUMNS},
        "warnings": [], "review_required": True,
        "inspection_mode": "class defaults / stored templates; no actor instantiation",
    }


def inspect_class(record, row_name, metadata, base_class, stage):
    data = empty_record(record, row_name)
    values, sources = data["csv_values"], data["field_sources"]
    if not record["compatible"]:
        data["warnings"].append("Registry metadata only: this class is outside the "
                                "BP_ProjectileBase family. Not loaded or instantiated; "
                                "omitted from the typed table CSV.")
        return data
    stage("loading class", record["blueprint"])
    cls = unreal.EditorAssetLibrary.load_blueprint_class(record["blueprint"])
    if not cls:
        raise RuntimeError("Blueprint class could not be loaded")
    if not unreal.MathLibrary.class_is_child_of(cls, base_class):
        data["compatible_with_BP_ProjectileBase"] = False
        data["warnings"].append("Loaded class ancestry disagrees with registry; excluded from row CSV.")
        return data
    stage("reading allowed class defaults", record["blueprint"])
    cdo = unreal.get_default_object(cls)
    allowed = [n for names in ALIASES.values() for n in names] + ["initial_life_span"]
    data["actor_defaults"] = read_allowed_properties(cdo, allowed)
    for column, names in ALIASES.items():
        if column == "Projectile Class":
            continue
        found, value, source = read_property(cdo, names)
        if found:
            try:
                values[column] = cell_value(column, value)
                sources[column] = "class default: " + source
            except Exception as exc:
                data["warnings"].append(column + " read/format failed: " + str(exc))
    values["Projectile Class"] = cls.get_path_name()
    sources["Projectile Class"] = "actual loaded source generated class"
    stage("reading stored component templates", record["blueprint"])
    try:
        templates = stored_templates(source_chain(record, metadata))
        data["components"] = [template_record(comp, owner) for comp, owner in templates]
        infer_template_fields(data, templates)
        data["components_inspected"] = bool(templates)
        if not templates:
            data["warnings"].append("No stored templates were discoverable. Component values "
                                    "are UNKNOWN, not proof that effects/lights are absent.")
    except Exception:
        data["warnings"].append("Template inspection incomplete: " + traceback.format_exc())
    if values["Speed"]:
        if float(values["Speed"]) <= 0:
            data["warnings"].append("Speed <= 0 in defaults; your old spawn node supplied "
                                    "600 at runtime. Check call-site overrides before import.")
    data["warnings"].append("Construction Scripts, BeginPlay, event graphs and spawn-pin "
                            "overrides are not evaluated. CSV requires review before import.")
    return data


def write_progress(output, phase, blueprint):
    with open(os.path.join(output, "Export_Progress.json"), "w", encoding="utf-8") as f:
        json.dump({"version": EXPORTER_VERSION, "phase": phase, "blueprint": blueprint}, f, indent=2)
        f.flush()
        os.fsync(f.fileno())
    unreal.log("Projectile v2: " + phase + " " + blueprint)


def run():
    if not hasattr(unreal, "EditorAssetLibrary"):
        raise RuntimeError("Enable Editor Scripting Utilities and restart the editor.")
    if unreal.EditorLevelLibrary.get_pie_worlds(True):
        raise RuntimeError("Stop Play/Simulate before exporting.")
    output = OUTPUT_DIRECTORY or os.path.join(unreal.Paths.project_saved_dir(),
        "ProjectileExports", "v2_" + datetime.datetime.now().strftime("%Y%m%d_%H%M%S_%f"))
    output = os.path.abspath(output)
    os.makedirs(output, exist_ok=True)
    stage = lambda phase, bp: write_progress(output, phase, bp)
    stage("reading registry", "")
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    candidates, metadata = metadata_candidates(registry)
    header = read_header(HEADER_TEMPLATE_CSV) if HEADER_TEMPLATE_CSV else ["---"] + COLUMNS
    header_source = "provided native CSV header" if HEADER_TEMPLATE_CSV else "screenshot field names; verify native CSV header before import"
    mapping, unknown = map_headers(header)
    names = unique_row_names([item["blueprint"] for item in candidates])
    records, errors = [], []
    write_outputs(output, header, mapping, unknown, header_source, records, errors)
    stage("loading projectile base class", BASE_BLUEPRINT)
    base = unreal.EditorAssetLibrary.load_blueprint_class(BASE_BLUEPRINT)
    if not base:
        raise RuntimeError("BP_ProjectileBase not found; check BASE_BLUEPRINT")
    for candidate in candidates:
        path = candidate["blueprint"]
        try:
            data = inspect_class(candidate, names[path], metadata, base, stage)
            records.append(data)
            with open(os.path.join(output, "Completed_Classes.jsonl"), "a", encoding="utf-8") as f:
                f.write(json.dumps(data, ensure_ascii=False, allow_nan=False) + "\n")
                f.flush()
                os.fsync(f.fileno())
        except Exception:
            error = {"blueprint": path, "error": traceback.format_exc()}
            errors.append(error)
            unreal.log_warning(str(error))
        write_outputs(output, header, mapping, unknown, header_source, records, errors)
    stage("complete", "")
    unreal.log("PROJECTILE V2 EXPORT COMPLETE: " + output)
    unreal.log_warning("No actors were instantiated. Review CSV blanks and report warnings before import.")
    return output


if __name__ == "__main__":
    run()
