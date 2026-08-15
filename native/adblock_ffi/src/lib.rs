use adblock::cosmetic_filter_cache::ProceduralOrActionFilter;
use adblock::engine::Engine;
use adblock::filters::cosmetic::{CosmeticFilterAction, CosmeticFilterOperator};
use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::resources::{MimeType, PermissionMask, Resource, ResourceType};
use base64::Engine as _;
use std::collections::{HashMap, HashSet};
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::ptr;
use std::sync::Mutex;

pub struct OmniAdblockEngine {
  engine: Mutex<Engine>,
  resources: Mutex<HashMap<String, StoredResource>>,
}

struct StoredResource {
  mime: String,
  content: Vec<u8>,
}

#[repr(C)]
pub struct OmniAdblockNetworkResult {
  pub should_block: i32,
  pub important: i32,
  pub redirect: *mut c_char,
  pub rewritten_url: *mut c_char,
}

#[repr(C)]
pub struct OmniAdblockCosmeticResult {
  pub hide_css: *mut c_char,
  pub injected_script: *mut c_char,
  pub exceptions_json: *mut c_char,
  pub generichide: i32,
}

fn cstr_to_str<'a>(p: *const c_char) -> Option<&'a str> {
  if p.is_null() {
    return None;
  }
  unsafe { CStr::from_ptr(p) }.to_str().ok()
}

fn to_cstring(s: &str) -> *mut c_char {
  CString::new(s).map(|c| c.into_raw()).unwrap_or(ptr::null_mut())
}

fn free_cstring(p: *mut c_char) {
  if !p.is_null() {
    unsafe {
      drop(CString::from_raw(p));
    }
  }
}

fn mime_label(kind: &ResourceType) -> String {
  match kind {
    ResourceType::Template => "application/javascript".into(),
    ResourceType::Mime(m) => match m {
      MimeType::ApplicationJavascript => "application/javascript".into(),
      MimeType::FnJavascript => "fn/javascript".into(),
      MimeType::TextHtml => "text/html".into(),
      MimeType::TextCss => "text/css".into(),
      MimeType::TextPlain => "text/plain".into(),
      MimeType::ImageGif => "image/gif".into(),
      MimeType::ImagePng => "image/png".into(),
      MimeType::ApplicationJson => "application/json".into(),
      MimeType::AudioMp3 => "audio/mp3".into(),
      MimeType::VideoMp4 => "video/mp4".into(),
      MimeType::TextXml => "text/xml".into(),
      MimeType::Unknown => "application/octet-stream".into(),
    },
  }
}

/// All bundled Omni lists are first-party / trusted (uBO, EasyList, Brave).
/// Bit 0 authorizes uBO `trusted-*` scriptlets (cookie clickers, set-cookie, …).
fn trusted_list_parse_options() -> ParseOptions {
  ParseOptions {
    permissions: PermissionMask::from_bits(0b0000_0001),
    ..ParseOptions::default()
  }
}

#[no_mangle]
pub extern "C" fn omni_adblock_create() -> *mut OmniAdblockEngine {
  let engine = OmniAdblockEngine {
    engine: Mutex::new(Engine::default()),
    resources: Mutex::new(HashMap::new()),
  };
  Box::into_raw(Box::new(engine))
}

#[no_mangle]
pub extern "C" fn omni_adblock_destroy(engine: *mut OmniAdblockEngine) {
  if engine.is_null() {
    return;
  }
  unsafe {
    drop(Box::from_raw(engine));
  }
}

#[no_mangle]
pub extern "C" fn omni_adblock_load_lists(
  engine: *mut OmniAdblockEngine,
  lists: *const *const c_char,
  lengths: *const usize,
  count: usize,
) -> i32 {
  if engine.is_null() || lists.is_null() {
    return -1;
  }
  let eng = unsafe { &*engine };
  let mut filter_set = FilterSet::new(false);
  let opts = trusted_list_parse_options();
  for i in 0..count {
    let ptr = unsafe { *lists.add(i) };
    if ptr.is_null() {
      continue;
    }
    let len = if lengths.is_null() {
      unsafe { CStr::from_ptr(ptr) }.to_bytes().len()
    } else {
      unsafe { *lengths.add(i) }
    };
    let bytes = unsafe { std::slice::from_raw_parts(ptr as *const u8, len) };
    let text = match std::str::from_utf8(bytes) {
      Ok(s) => s,
      Err(_) => continue,
    };
    filter_set.add_filter_list(text.to_string(), opts.clone());
  }
  match eng.engine.lock() {
    Ok(mut e) => {
      *e = Engine::new_with_filter_set(filter_set);
      0
    }
    Err(_) => -2,
  }
}

#[no_mangle]
pub extern "C" fn omni_adblock_load_resources_json(
  engine: *mut OmniAdblockEngine,
  json: *const c_char,
  length: usize,
) -> i32 {
  if engine.is_null() || json.is_null() {
    return -1;
  }
  let eng = unsafe { &*engine };
  let bytes = unsafe { std::slice::from_raw_parts(json as *const u8, length) };
  let text = match std::str::from_utf8(bytes) {
    Ok(s) => s,
    Err(_) => return -3,
  };
  // Deserialize Brave/adblock-rust Resource JSON directly so dependencies,
  // permission masks, and fn/javascript kinds are preserved.
  let resources: Vec<Resource> = match serde_json::from_str(text) {
    Ok(v) => v,
    Err(_) => return -4,
  };

  let mut map = HashMap::new();
  for item in &resources {
    let mime = mime_label(&item.kind);
    let content = base64::engine::general_purpose::STANDARD
      .decode(item.content.as_bytes())
      .unwrap_or_default();
    let stored = StoredResource {
      mime: mime.clone(),
      content: content.clone(),
    };
    map.insert(item.name.clone(), stored);
    for alias in &item.aliases {
      map.insert(
        alias.clone(),
        StoredResource {
          mime: mime.clone(),
          content: content.clone(),
        },
      );
    }
  }

  if let Ok(mut store) = eng.resources.lock() {
    *store = map;
  }
  if let Ok(mut e) = eng.engine.lock() {
    e.use_resources(resources);
    0
  } else {
    -2
  }
}

#[no_mangle]
pub extern "C" fn omni_adblock_check_network(
  engine: *mut OmniAdblockEngine,
  url: *const c_char,
  source_url: *const c_char,
  request_type: *const c_char,
  method: *const c_char,
) -> OmniAdblockNetworkResult {
  let empty = OmniAdblockNetworkResult {
    should_block: 0,
    important: 0,
    redirect: ptr::null_mut(),
    rewritten_url: ptr::null_mut(),
  };
  if engine.is_null() {
    return empty;
  }
  let url = match cstr_to_str(url) {
    Some(s) => s,
    None => return empty,
  };
  let source = cstr_to_str(source_url).unwrap_or("");
  let rtype = cstr_to_str(request_type).unwrap_or("other");
  let method = cstr_to_str(method).unwrap_or("GET");
  let method = if method.is_empty() { "GET" } else { method };
  let req = match Request::new(url, source, rtype, method) {
    Ok(r) => r,
    Err(_) => return empty,
  };
  let eng = unsafe { &*engine };
  let guard = match eng.engine.lock() {
    Ok(g) => g,
    Err(_) => return empty,
  };
  let result = guard.check_network_request(&req);
  OmniAdblockNetworkResult {
    should_block: if result.should_block() { 1 } else { 0 },
    important: if result.important { 1 } else { 0 },
    redirect: result
      .redirect
      .as_deref()
      .map(to_cstring)
      .unwrap_or(ptr::null_mut()),
    rewritten_url: result
      .rewritten_url
      .as_deref()
      .map(to_cstring)
      .unwrap_or(ptr::null_mut()),
  }
}

fn parse_json_string_array(json: &str) -> Vec<String> {
  serde_json::from_str::<Vec<String>>(json).unwrap_or_default()
}

fn append_style_rules(css: &mut String, rules: &[(String, String)]) {
  // Group selectors that share the same style declaration.
  let mut by_style: HashMap<String, Vec<String>> = HashMap::new();
  for (selector, style) in rules {
    by_style
      .entry(style.clone())
      .or_default()
      .push(selector.clone());
  }
  for (style, selectors) in by_style {
    if selectors.is_empty() {
      continue;
    }
    css.push_str(&selectors.join(",\n"));
    css.push_str(" { ");
    css.push_str(&style);
    if !style.trim_end().ends_with(';') {
      css.push(';');
    }
    css.push_str(" }\n");
  }
}

#[no_mangle]
pub extern "C" fn omni_adblock_cosmetics(
  engine: *mut OmniAdblockEngine,
  url: *const c_char,
) -> OmniAdblockCosmeticResult {
  let empty = OmniAdblockCosmeticResult {
    hide_css: ptr::null_mut(),
    injected_script: ptr::null_mut(),
    exceptions_json: ptr::null_mut(),
    generichide: 0,
  };
  if engine.is_null() {
    return empty;
  }
  let url = match cstr_to_str(url) {
    Some(s) => s,
    None => return empty,
  };
  let eng = unsafe { &*engine };
  let guard = match eng.engine.lock() {
    Ok(g) => g,
    Err(_) => return empty,
  };
  let resources = guard.url_cosmetic_resources(url);
  let mut style_rules: Vec<(String, String)> = Vec::new();
  if !resources.hide_selectors.is_empty() {
    for selector in &resources.hide_selectors {
      // List hide rules stay display:none only. Extra height:0 on every
      // selector would fight :style() unhide/layout fixes on the same node.
      style_rules.push((selector.clone(), "display: none !important".into()));
    }
  }
  // Brave/uBO :style(...) collapse leftovers, plus :remove() as hide.
  for action_json in &resources.procedural_actions {
    if let Ok(filter) = serde_json::from_str::<ProceduralOrActionFilter>(action_json) {
      if let Some((selector, style)) = filter.as_css() {
        style_rules.push((selector, style));
      } else if let (
        [CosmeticFilterOperator::CssSelector(selector)],
        Some(CosmeticFilterAction::Remove),
      ) = (&filter.selector[..], &filter.action)
      {
        style_rules.push((selector.clone(), "display: none !important".into()));
      }
    }
  }
  let mut css = String::new();
  append_style_rules(&mut css, &style_rules);
  let exceptions_json = serde_json::to_string(
    &resources.exceptions.iter().cloned().collect::<Vec<_>>(),
  )
  .unwrap_or_else(|_| "[]".into());
  OmniAdblockCosmeticResult {
    hide_css: if css.is_empty() {
      ptr::null_mut()
    } else {
      to_cstring(&css)
    },
    injected_script: if resources.injected_script.is_empty() {
      ptr::null_mut()
    } else {
      to_cstring(&resources.injected_script)
    },
    exceptions_json: to_cstring(&exceptions_json),
    generichide: if resources.generichide { 1 } else { 0 },
  }
}

#[no_mangle]
pub extern "C" fn omni_adblock_hidden_class_id_css(
  engine: *mut OmniAdblockEngine,
  classes_json: *const c_char,
  ids_json: *const c_char,
  exceptions_json: *const c_char,
) -> *mut c_char {
  if engine.is_null() {
    return ptr::null_mut();
  }
  let classes = parse_json_string_array(cstr_to_str(classes_json).unwrap_or("[]"));
  let ids = parse_json_string_array(cstr_to_str(ids_json).unwrap_or("[]"));
  let exceptions_vec =
    parse_json_string_array(cstr_to_str(exceptions_json).unwrap_or("[]"));
  let exceptions: HashSet<String> = exceptions_vec.into_iter().collect();
  if classes.is_empty() && ids.is_empty() {
    return ptr::null_mut();
  }
  let eng = unsafe { &*engine };
  let guard = match eng.engine.lock() {
    Ok(g) => g,
    Err(_) => return ptr::null_mut(),
  };
  let selectors = guard.hidden_class_id_selectors(&classes, &ids, &exceptions);
  if selectors.is_empty() {
    return ptr::null_mut();
  }
  let mut css = String::new();
  css.push_str(&selectors.join(",\n"));
  css.push_str(" { display: none !important; }\n");
  to_cstring(&css)
}

#[no_mangle]
pub extern "C" fn omni_adblock_resource_data_url(
  engine: *mut OmniAdblockEngine,
  name: *const c_char,
) -> *mut c_char {
  if engine.is_null() {
    return ptr::null_mut();
  }
  let name = match cstr_to_str(name) {
    Some(s) => s,
    None => return ptr::null_mut(),
  };
  let eng = unsafe { &*engine };
  let store = match eng.resources.lock() {
    Ok(g) => g,
    Err(_) => return ptr::null_mut(),
  };
  let Some(res) = store.get(name) else {
    return ptr::null_mut();
  };
  let b64 = base64_encode(&res.content);
  let url = format!("data:{};base64,{}", res.mime, b64);
  to_cstring(&url)
}

fn base64_encode(data: &[u8]) -> String {
  const T: &[u8] =
    b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  let mut out = String::with_capacity((data.len() + 2) / 3 * 4);
  for chunk in data.chunks(3) {
    let a = chunk[0] as u32;
    let b = chunk.get(1).copied().unwrap_or(0) as u32;
    let c = chunk.get(2).copied().unwrap_or(0) as u32;
    let triple = (a << 16) | (b << 8) | c;
    out.push(T[((triple >> 18) & 63) as usize] as char);
    out.push(T[((triple >> 12) & 63) as usize] as char);
    if chunk.len() > 1 {
      out.push(T[((triple >> 6) & 63) as usize] as char);
    } else {
      out.push('=');
    }
    if chunk.len() > 2 {
      out.push(T[(triple & 63) as usize] as char);
    } else {
      out.push('=');
    }
  }
  out
}

#[no_mangle]
pub extern "C" fn omni_adblock_string_free(s: *mut c_char) {
  free_cstring(s);
}

#[no_mangle]
pub extern "C" fn omni_adblock_network_result_free(r: *mut OmniAdblockNetworkResult) {
  if r.is_null() {
    return;
  }
  unsafe {
    free_cstring((*r).redirect);
    free_cstring((*r).rewritten_url);
    (*r).redirect = ptr::null_mut();
    (*r).rewritten_url = ptr::null_mut();
  }
}

#[no_mangle]
pub extern "C" fn omni_adblock_cosmetic_result_free(r: *mut OmniAdblockCosmeticResult) {
  if r.is_null() {
    return;
  }
  unsafe {
    free_cstring((*r).hide_css);
    free_cstring((*r).injected_script);
    free_cstring((*r).exceptions_json);
    (*r).hide_css = ptr::null_mut();
    (*r).injected_script = ptr::null_mut();
    (*r).exceptions_json = ptr::null_mut();
  }
}
