import { ArdorApiError } from "../api/errors";

const REFUSED = "The pedal refused the login. Check the user name and password.";
const FALLBACK = "Could not sign in to the pedal.";

/** A 401 gets the specific copy. Any other error keeps its own message. */
export function loginErrorMessage(reason: unknown): string {
  if (reason instanceof ArdorApiError && reason.status === 401) return REFUSED;
  return reason instanceof Error ? reason.message : FALLBACK;
}
