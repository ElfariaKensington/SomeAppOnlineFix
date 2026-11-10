import AppKit
import Darwin
import Foundation

enum ValheimPatch {
    private static func check(_ result: Int32) throws {
        guard result != 0 else { return }
        let detail: String
        switch result {
        case ENOENT: detail = "Required game files are missing. Verify the installation in Steam."
        case ENOTSUP: detail = "Game file hashes or structure do not match this fix. Patch blocked."
        case ESTALE: detail = "Game files or backups do not match. Keep the Valheim patch backups for recovery."
        case EWOULDBLOCK: detail = "Another Valheim patch operation is running."
        case EIO: detail = "Writing or verifying the patch failed. Keep the Valheim patch backups for recovery."
        default: detail = String(cString: strerror(result))
        }
        throw ManagerError(message: "Valheim PlayFab fix: \(detail)")
    }

    static func isEnabled(app: URL) throws -> Bool {
        var enabled = false
        try check(app.path.withCString { of_valheim_status($0, &enabled) })
        return enabled
    }

    static func setEnabled(_ enabled: Bool, app: URL) throws {
        guard !NSRunningApplication.runningApplications(withBundleIdentifier: "com.coffeestain.valheim-steam")
            .contains(where: { $0.bundleURL?.resolvingSymlinksInPath() == app.resolvingSymlinksInPath() }) else {
            throw ManagerError(message: "Quit Valheim first.")
        }
        try check(app.path.withCString { of_valheim_patch($0, enabled) })
    }
}
