// hid-enum.swift — Lista todas las interfaces HID del teclado Corsair (VID 0x1B1C).
// Objetivo: descubrir cuál interfaz (usage page / usage) es la de "control"
// para poder abrirla y mandarle comandos de color. Cero dependencias externas.
//
// Compilar:  swiftc hid-enum.swift -o hid-enum
// Correr:    ./hid-enum

import Foundation
import IOKit
import IOKit.hid

let CORSAIR_VID = 0x1B1C

func intProp(_ device: IOHIDDevice, _ key: String) -> Int? {
    return IOHIDDeviceGetProperty(device, key as CFString) as? Int
}
func strProp(_ device: IOHIDDevice, _ key: String) -> String? {
    return IOHIDDeviceGetProperty(device, key as CFString) as? String
}
func hex(_ v: Int, _ width: Int = 4) -> String {
    return String(format: "0x%0\(width)X", v)
}

let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
let matching: [String: Any] = [kIOHIDVendorIDKey as String: CORSAIR_VID]
IOHIDManagerSetDeviceMatching(manager, matching as CFDictionary)

let openRes = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))
if openRes != kIOReturnSuccess {
    FileHandle.standardError.write(
        Data("aviso: IOHIDManagerOpen devolvió \(hex(Int(openRes), 8)) (seguimos, la enumeración suele funcionar igual)\n".utf8))
}

guard let devices = IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice>, !devices.isEmpty else {
    print("No se encontró ningún HID de Corsair (VID \(hex(CORSAIR_VID))). ¿Está conectado el teclado?")
    exit(1)
}

// Ordenar por usage page/usage para leerlo cómodo.
let sorted = devices.sorted { a, b in
    let pa = intProp(a, kIOHIDPrimaryUsagePageKey) ?? 0
    let pb = intProp(b, kIOHIDPrimaryUsagePageKey) ?? 0
    if pa != pb { return pa < pb }
    return (intProp(a, kIOHIDPrimaryUsageKey) ?? 0) < (intProp(b, kIOHIDPrimaryUsageKey) ?? 0)
}

print("Encontradas \(sorted.count) interfaz(es) HID para VID \(hex(CORSAIR_VID)):\n")
for (i, d) in sorted.enumerated() {
    let name    = strProp(d, kIOHIDProductKey) ?? "?"
    let vid     = intProp(d, kIOHIDVendorIDKey) ?? 0
    let pid     = intProp(d, kIOHIDProductIDKey) ?? 0
    let upage   = intProp(d, kIOHIDPrimaryUsagePageKey) ?? 0
    let usage   = intProp(d, kIOHIDPrimaryUsageKey) ?? 0
    let loc     = intProp(d, kIOHIDLocationIDKey) ?? 0
    let maxIn   = intProp(d, kIOHIDMaxInputReportSizeKey) ?? 0
    let maxOut  = intProp(d, kIOHIDMaxOutputReportSizeKey) ?? 0
    let maxFeat = intProp(d, kIOHIDMaxFeatureReportSizeKey) ?? 0

    // Pista: la interfaz de teclado real usa usage page 0x01 (Generic Desktop).
    // La de control de Corsair suele estar en una usage page "vendor" (0xFF00+).
    let esTeclado = upage == 0x01
    let pista = esTeclado ? "  <- interfaz de tecleo (NO usar)"
             : (upage >= 0xFF00 ? "  <- probable interfaz de CONTROL (vendor)" : "")

    print("[\(i)] \(name)\(pista)")
    print("    PID \(hex(pid))  usagePage \(hex(upage))  usage \(hex(usage, 2))  loc \(hex(loc, 8))")
    print("    reportSize  in=\(maxIn)  out=\(maxOut)  feature=\(maxFeat)")
    print("")
}
