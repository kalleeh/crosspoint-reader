#pragma once

#include "../Activity.h"
#include "../../MappedInputManager.h"
#include <GfxRenderer.h>
#include <functional>
#include <vector>

class AWSCertMenuActivity final : public Activity {
 public:
  enum class Tab { Certifications, Stats };
  
 private:
  const std::function<void()> onBack;
  const std::function<void(const char*)> onSelectCert;
  
  Tab currentTab = Tab::Certifications;
  
  struct CertOption {
    const char* id;
    const char* name;
    const char* examCode;
    int questionCount;
    int passingScore;
    const char* level;
    const char* audience;
    const char* domains;
  };
  
  std::vector<CertOption> certs;
  int selectedIndex = 0;
  int scrollOffset = 0;
  bool showingInfo = false;
  std::vector<bool> certFileAvailable;
  
  void render();
  void renderCertificationsTab();
  void renderStatsTab();
  void renderInfo();
  
 public:
  explicit AWSCertMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const std::function<void()>& onBack,
                               const std::function<void(const char*)>& onSelectCert)
      : Activity("AWS Certifications", renderer, mappedInput), 
        onBack(onBack), onSelectCert(onSelectCert) {
    // Foundational
    certs.push_back({"cloud-practitioner", "Cloud Practitioner", "CLF-C02", 65, 70,
                     "Foundational", "Business, Sales, Technical",
                     "Cloud Concepts 24% | Security 30% | Technology 34% | Billing 12%"});
    certs.push_back({"ai-practitioner", "AI Practitioner", "AIF-C01", 65, 70,
                     "Foundational", "Business, Technical, AI/ML interested",
                     "AI/ML Fundamentals 20% | Generative AI 24% | Applications 28% | Security 28%"});
    
    // Associate
    certs.push_back({"sa-associate", "Solutions Architect Associate", "SAA-C03", 65, 72,
                     "Associate", "Solutions Architects, 1yr AWS experience",
                     "Resilient Arch 26% | High-Performance 24% | Secure Apps 30% | Cost-Optimized 20%"});
    certs.push_back({"developer-associate", "Developer Associate", "DVA-C02", 65, 72,
                     "Associate", "Developers, 1yr AWS development",
                     "Development 32% | Security 26% | Deployment 24% | Troubleshooting 18%"});
    certs.push_back({"sysops-associate", "SysOps Administrator Associate", "SOA-C02", 65, 72,
                     "Associate", "SysOps, 1yr AWS operations",
                     "Monitoring 20% | Reliability 16% | Deployment 18% | Security 16% | Networking 18% | Cost 12%"});
    
    // Professional
    certs.push_back({"sa-professional", "Solutions Architect Professional", "SAP-C02", 75, 75,
                     "Professional", "Solutions Architects, 2yr AWS experience",
                     "Design Solutions 26% | Continuous Improvement 26% | Migration 18% | Cost Control 20% | Security 10%"});
    certs.push_back({"devops-professional", "DevOps Engineer Professional", "DOP-C02", 75, 75,
                     "Professional", "DevOps Engineers, 2yr AWS experience",
                     "SDLC Automation 22% | Config Mgmt 17% | Monitoring 15% | Policies 10% | Incident Response 14% | Security 22%"});
    
    // Specialty
    certs.push_back({"security-specialty", "Security Specialty", "SCS-C02", 65, 75,
                     "Specialty", "Security roles, 2yr AWS security",
                     "Threat Detection 14% | Security Logging 18% | Infrastructure 20% | Identity 16% | Data Protection 18% | Incident Response 14%"});
    certs.push_back({"ml-specialty", "Machine Learning Specialty", "MLS-C01", 65, 75,
                     "Specialty", "ML Engineers, 1yr AWS ML experience",
                     "Data Engineering 20% | Exploratory Analysis 24% | Modeling 36% | ML Implementation 20%"});
    certs.push_back({"database-specialty", "Database Specialty", "DBS-C01", 65, 75,
                     "Specialty", "Database Architects, 2yr AWS DB",
                     "Workload Design 26% | Deployment 20% | Management 18% | Monitoring 18% | Security 18%"});
    certs.push_back({"networking-specialty", "Advanced Networking Specialty", "ANS-C01", 65, 75,
                     "Specialty", "Network Engineers, 5yr networking",
                     "Network Design 30% | Implementation 26% | Management 20% | Security 24%"});
    certs.push_back({"analytics-specialty", "Data Analytics Specialty", "DAS-C01", 65, 75,
                     "Specialty", "Data Analysts, 2yr AWS analytics",
                     "Collection 18% | Storage 22% | Processing 24% | Analysis 18% | Visualization 12% | Security 6%"});
    certs.push_back({"sap-specialty", "SAP on AWS Specialty", "PAS-C01", 65, 75,
                     "Specialty", "SAP Architects, SAP + AWS experience",
                     "SAP Workloads 30% | Design 28% | Implementation 20% | Operations 22%"});
  }
  
  void onEnter() override;
  void loop() override;
};
