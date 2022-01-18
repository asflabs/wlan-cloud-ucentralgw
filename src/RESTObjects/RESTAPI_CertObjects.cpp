// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
//
// Created by stephane bourque on 2021-12-07.
//

#include "RESTAPI_CertObjects.h"

using OpenWifi::RESTAPI_utils::field_to_json;
using OpenWifi::RESTAPI_utils::field_from_json;

namespace OpenWifi {
    namespace  CertObjects {
        void CertificateEntry::to_json(Poco::JSON::Object &Obj) const {
            field_to_json(Obj,"id", id);
            field_to_json(Obj,"entity", entity);
            field_to_json(Obj,"creator", creator);
            field_to_json(Obj,"type", type);
            field_to_json(Obj,"status", status);
            field_to_json(Obj,"certificate", certificate);
            field_to_json(Obj,"key", key);
            field_to_json(Obj,"devid", devid);
            field_to_json(Obj,"cas", cas);
            field_to_json(Obj,"manufacturer", manufacturer);
            field_to_json(Obj,"model", model);
            field_to_json(Obj,"redirector", redirector);
            field_to_json(Obj,"commonName", commonName);
            field_to_json(Obj,"certificateId", certificateId);
            field_to_json(Obj,"batch", batch);
            field_to_json(Obj,"created", created);
            field_to_json(Obj,"modified", modified);
            field_to_json(Obj,"revoked", revoked);
            field_to_json(Obj,"revokeCount", revokeCount);
        }

        bool CertificateEntry::from_json(const Poco::JSON::Object::Ptr &Obj) {
            try {
                field_from_json(Obj,"id", id);
                field_from_json(Obj,"entity", entity);
                field_from_json(Obj,"creator", creator);
                field_from_json(Obj,"type", type);
                field_from_json(Obj,"status", status);
                field_from_json(Obj,"certificate", certificate);
                field_from_json(Obj,"key", key);
                field_from_json(Obj,"devid", devid);
                field_from_json(Obj,"cas", cas);
                field_from_json(Obj,"manufacturer", manufacturer);
                field_from_json(Obj,"model", model);
                field_from_json(Obj,"redirector", redirector);
                field_from_json(Obj,"commonName", commonName);
                field_from_json(Obj,"certificateId", certificateId);
                field_from_json(Obj,"batch", batch);
                field_from_json(Obj,"created", created);
                field_from_json(Obj,"modified", modified);
                field_from_json(Obj,"revoked", revoked);
                field_from_json(Obj,"revokeCount", revokeCount);
                return true;
            } catch (...) {
            }
            return false;
        }

        void EntityEntry::to_json(Poco::JSON::Object &Obj) const {
            field_to_json(Obj,"id", id);
            field_to_json(Obj,"creator", creator);
            field_to_json(Obj,"name", name);
            field_to_json(Obj,"description", description);
            field_to_json(Obj,"defaultRedirector", defaultRedirector);
            field_to_json(Obj,"apiKey", apiKey);
            field_to_json(Obj,"serverEnrollmentProfile", serverEnrollmentProfile);
            field_to_json(Obj,"clientEnrollmentProfile", clientEnrollmentProfile);
            field_to_json(Obj,"organization", organization);
            field_to_json(Obj,"created", created);
            field_to_json(Obj,"modified", modified);
            field_to_json(Obj,"suspended", suspended);
            field_to_json(Obj,"deleted", deleted);
            field_to_json(Obj,"notes", notes);
        }

        bool EntityEntry::from_json(const Poco::JSON::Object::Ptr &Obj) {
            try {
                field_from_json(Obj,"id", id);
                field_from_json(Obj,"creator", creator);
                field_from_json(Obj,"name", name);
                field_from_json(Obj,"description", description);
                field_from_json(Obj,"defaultRedirector", defaultRedirector);
                field_from_json(Obj,"apiKey", apiKey);
                field_from_json(Obj,"serverEnrollmentProfile", serverEnrollmentProfile);
                field_from_json(Obj,"clientEnrollmentProfile", clientEnrollmentProfile);
                field_from_json(Obj,"organization", organization);
                field_from_json(Obj,"created", created);
                field_from_json(Obj,"modified", modified);
                field_from_json(Obj,"suspended", suspended);
                field_from_json(Obj,"deleted", deleted);
                field_from_json(Obj,"notes", notes);
                return true;
            } catch (...) {
            }
            return false;
        }

        void BatchEntry::to_json(Poco::JSON::Object &Obj) const {
            field_to_json(Obj,"id", id);
            field_to_json(Obj,"entity", entity);
            field_to_json(Obj,"creator", creator);
            field_to_json(Obj,"name", name);
            field_to_json(Obj,"description", description);
            field_to_json(Obj,"manufacturer", manufacturer);
            field_to_json(Obj,"model", model);
            field_to_json(Obj,"redirector", redirector);
            field_to_json(Obj,"commonNames", commonNames);
            field_to_json(Obj,"jobHistory", jobHistory);
            field_to_json(Obj,"notes", notes);
            field_to_json(Obj,"submitted", submitted);
            field_to_json(Obj,"started", started);
            field_to_json(Obj,"completed", completed);
            field_to_json(Obj,"modified", modified);
        }

        bool BatchEntry::from_json(const Poco::JSON::Object::Ptr &Obj) {
            try {
                field_from_json(Obj,"id", id);
                field_from_json(Obj,"entity", entity);
                field_from_json(Obj,"creator", creator);
                field_from_json(Obj,"name", name);
                field_from_json(Obj,"description", description);
                field_from_json(Obj,"manufacturer", manufacturer);
                field_from_json(Obj,"model", model);
                field_from_json(Obj,"redirector", redirector);
                field_from_json(Obj,"commonNames", commonNames);
                field_from_json(Obj,"jobHistory", jobHistory);
                field_from_json(Obj,"notes", notes);
                field_from_json(Obj,"submitted", submitted);
                field_from_json(Obj,"started", started);
                field_from_json(Obj,"completed", completed);
                field_from_json(Obj,"modified", modified);
                return true;
            } catch (...) {
            }
            return false;
        }

        void JobEntry::to_json(Poco::JSON::Object &Obj) const {
            field_to_json(Obj,"id", id);
            field_to_json(Obj,"entity", entity);
            field_to_json(Obj,"creator", creator);
            field_to_json(Obj,"batch", batch);
            field_to_json(Obj,"commonNames", commonNames);
            field_to_json(Obj,"completedNames", completedNames);
            field_to_json(Obj,"errorNames", errorNames);
            field_to_json(Obj,"status", status);
            field_to_json(Obj,"command", command);
            field_to_json(Obj,"parameters", parameters);
            field_to_json(Obj,"submitted", submitted);
            field_to_json(Obj,"started", started);
            field_to_json(Obj,"completed", completed);
        }

        bool JobEntry::from_json(const Poco::JSON::Object::Ptr &Obj) {
            try {
                field_from_json(Obj,"id", id);
                field_from_json(Obj,"entity", entity);
                field_from_json(Obj,"creator", creator);
                field_from_json(Obj,"batch", batch);
                field_from_json(Obj,"commonNames", commonNames);
                field_from_json(Obj,"completedNames", completedNames);
                field_from_json(Obj,"errorNames", errorNames);
                field_from_json(Obj,"status", status);
                field_from_json(Obj,"command", command);
                field_from_json(Obj,"parameters", parameters);
                field_from_json(Obj,"submitted", submitted);
                field_from_json(Obj,"started", started);
                field_from_json(Obj,"completed", completed);
                return true;
            } catch (...) {
            }
            return false;
        }
    }
}
