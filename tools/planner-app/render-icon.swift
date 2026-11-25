import AppKit

let directory = URL(fileURLWithPath: CommandLine.arguments[1])
for size in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let pixels = size * scale
        let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels,
                                      bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                                      isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        let context = NSGraphicsContext(bitmapImageRep: bitmap)!
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = context
        context.cgContext.scaleBy(x: CGFloat(pixels) / 512, y: CGFloat(pixels) / 512)
        NSColor(calibratedRed: 0.89, green: 0.93, blue: 0.85, alpha: 1).setFill()
        NSBezierPath(roundedRect: NSRect(x: 22, y: 22, width: 468, height: 468), xRadius: 105, yRadius: 105).fill()
        NSColor(calibratedRed: 0.18, green: 0.36, blue: 0.28, alpha: 1).setFill()
        let castle = NSBezierPath()
        castle.move(to: NSPoint(x: 105, y: 122))
        for (x, y) in [(105, 382), (159, 382), (159, 344), (210, 344), (210, 395),
                       (302, 395), (302, 344), (353, 344), (353, 382), (407, 382), (407, 122)] {
            castle.line(to: NSPoint(x: x, y: y))
        }
        castle.close()
        castle.fill()
        for (x, y) in [(182, 218), (256, 275), (330, 195)] {
            NSColor(calibratedRed: 0.70, green: 0.80, blue: 0.64, alpha: 1).setFill()
            NSBezierPath(roundedRect: NSRect(x: x - 5, y: 164, width: 10, height: 150), xRadius: 5, yRadius: 5).fill()
            NSColor(calibratedRed: 0.91, green: 0.76, blue: 0.43, alpha: 1).setFill()
            NSBezierPath(roundedRect: NSRect(x: x - 17, y: y - 13, width: 34, height: 26), xRadius: 5, yRadius: 5).fill()
        }
        NSGraphicsContext.restoreGraphicsState()
        let suffix = scale == 2 ? "@2x" : ""
        let output = directory.appendingPathComponent("icon_\(size)x\(size)\(suffix).png")
        try bitmap.representation(using: .png, properties: [:])!.write(to: output)
    }
}
