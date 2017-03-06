/*! Definition of SmartTree class.
This file is part of https://github.com/hh-italian-group/AnalysisTools. */

#pragma once

#include <stdexcept>
#include <sstream>
#include <memory>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <mutex>

#include <TFile.h>
#include <TTree.h>
#include <Rtypes.h>

#define DECLARE_BRANCH_VARIABLE(type, name) type name;
#define ADD_DATA_TREE_BRANCH(name) AddBranch(#name, _data->name);

#define DECLARE_TREE(namespace_name, data_class_name, tree_class_name, data_macro, tree_name) \
    namespace namespace_name { \
    struct data_class_name : public root_ext::detail::BaseDataClass { data_macro() }; \
    using data_class_name##Vector = std::vector< data_class_name >; \
    class tree_class_name : public root_ext::detail::BaseSmartTree<data_class_name> { \
    public: \
        static const std::string& Name() { static const std::string name = tree_name; return name; } \
        tree_class_name(TDirectory* directory, bool readMode, const std::set<std::string>& disabled_branches = {}, \
                        const std::set<std::string>& enabled_branches = {}) \
            : BaseSmartTree(Name(), directory, readMode, disabled_branches,enabled_branches) { Initialize(); } \
        tree_class_name(const std::string& name, TDirectory* directory, bool readMode, \
                        const std::set<std::string>& disabled_branches = {}, \
                        const std::set<std::string>& enabled_branches = {}) \
            : BaseSmartTree(name, directory, readMode, disabled_branches,enabled_branches) { Initialize(); } \
    private: \
        inline void Initialize(); \
    }; \
    } \
    /**/

#define INITIALIZE_TREE(namespace_name, tree_class_name, data_macro) \
    namespace namespace_name { \
        inline void tree_class_name::Initialize() { \
            data_macro() \
            if (GetEntries() > 0) GetEntry(0); \
        } \
    } \
    /**/

namespace root_ext {
template<typename type>
using strmap = std::map<std::string, type>;
template<typename type>
using intmap = std::map<uint32_t, type>;

namespace detail {
    struct BaseSmartTreeEntry {
        virtual ~BaseSmartTreeEntry() {}
        virtual void clear() {}
    };

    template<typename DataType>
    struct SmartTreePtrEntry : BaseSmartTreeEntry {
        DataType* value;
        SmartTreePtrEntry(DataType& origin)
            : value(&origin) {}
    };

    template<typename DataType>
    struct SmartTreeCollectionEntry : SmartTreePtrEntry<DataType> {
        using SmartTreePtrEntry<DataType>::SmartTreePtrEntry;
        virtual void clear() override { this->value->clear(); }
    };

    template<typename DataType>
    struct EntryTypeSelector { using PtrEntry = SmartTreePtrEntry<DataType>; };
    template<typename DataType>
    struct EntryTypeSelector<std::vector<DataType>> {
        using PtrEntry = SmartTreeCollectionEntry<std::vector<DataType>>;
    };
    template<typename KeyType, typename DataType>
    struct EntryTypeSelector<std::map<KeyType, DataType>> {
        using PtrEntry = SmartTreeCollectionEntry<std::map<KeyType, DataType>>;
    };

    struct BaseDataClass {
        virtual ~BaseDataClass() {}
    };

    using SmartTreeEntryMap = std::unordered_map<std::string, std::shared_ptr<BaseSmartTreeEntry>>;

    inline void EnableBranch(TBranch& branch)
    {
        branch.SetStatus(1);
        auto sub_branches = branch.GetListOfBranches();
        for(auto sub_branch : *sub_branches)
            EnableBranch(*dynamic_cast<TBranch*>(sub_branch));
    }

    inline void EnableBranch(TTree& tree, const std::string& branch_name)
    {
        TBranch* branch = tree.GetBranch(branch_name.c_str());
        if(!branch) {
            std::ostringstream ss;
            ss << "Branch '" << branch_name << "' not found.";
            throw std::runtime_error(ss.str());
        }
        EnableBranch(*branch);
    }

    template<typename DataType>
    struct BranchCreator {
        void Create(TTree& tree, const std::string& branch_name, DataType& value, bool readMode,
                    SmartTreeEntryMap& entries)
        {
            using FixesMap = std::unordered_map<std::string, std::string>;

            static const FixesMap class_fixes = {
                { "ROOT::Math::LorentzVector<ROOT::Math::PtEtaPhiM4D<Double32_t> >",
                  "ROOT::Math::LorentzVector<ROOT::Math::PtEtaPhiM4D<double> >" }
            };

            auto get_fixed_name = [&](const std::string& cl_name, const std::string& branch_cl_name) -> std::string {
                if(cl_name == branch_cl_name) return branch_cl_name;
                if(class_fixes.count(cl_name) && class_fixes.at(cl_name) == branch_cl_name) return branch_cl_name;
                auto iter = std::find_if(class_fixes.begin(), class_fixes.end(),
                    [&](const FixesMap::value_type& pair) -> bool { return pair.second == cl_name; });
                if(iter != class_fixes.end() && iter->first == branch_cl_name) return branch_cl_name;
                std::ostringstream ss;
                ss << "The pointer type given (" << cl_name << ") does not correspond to the class needed ("
                   << branch_cl_name << ") by the branch: " << branch_name << ".";
                throw std::runtime_error(ss.str());
            };

            using PtrEntry = typename EntryTypeSelector<DataType>::PtrEntry;
            std::shared_ptr<PtrEntry> entry(new PtrEntry(value));
            if(entries.count(branch_name))
                throw std::runtime_error("Entry is already defined.");
            entries[branch_name] = entry;

            TClass *cl = TClass::GetClass(typeid(DataType));

            if(readMode) {
                try {
                    EnableBranch(tree, branch_name);

                    if(cl) {
                        TBranch* branch = tree.GetBranch(branch_name.c_str());
                        const std::string fixed_name = get_fixed_name(cl->GetName(), branch->GetClassName());
                        cl = TClass::GetClass(fixed_name.c_str());
                        tree.SetBranchAddress(branch_name.c_str(), &entry->value, nullptr, cl, kOther_t, true);
                    } else
                        tree.SetBranchAddress(branch_name.c_str(), entry->value);
                    if(tree.GetReadEntry() >= 0)
                        tree.GetBranch(branch_name.c_str())->GetEntry(tree.GetReadEntry());
                } catch(std::runtime_error& error) {
                    std::cerr << "ERROR: " << error.what() << std::endl;
                }
            } else {

                TBranch* branch;
                if(cl) {
                    std::string cl_name = cl->GetName();
                    if(class_fixes.count(cl_name))
                        cl_name = class_fixes.at(cl_name);
                    branch = tree.Branch(branch_name.c_str(), cl_name.c_str(), entry->value);
                } else
                    branch = tree.Branch(branch_name.c_str(), entry->value);

                const Long64_t n_entries = tree.GetEntries();
                for(Long64_t n = 0; n < n_entries; ++n)
                    branch->Fill();
            }
        }
    };

} // detail

class SmartTree {
public:
    SmartTree(const std::string& _name, TDirectory* _directory, bool _readMode,
              const std::set<std::string>& _disabled_branches = {}, const std::set<std::string>& _enabled_branches ={})
        : name(_name), directory(_directory), readMode(_readMode), disabled_branches(_disabled_branches),
          enabled_branches(_enabled_branches)
    {
        static constexpr Long64_t maxVirtualSize = 100000000;

        if(readMode) {
            if(!directory)
                throw std::runtime_error("Can't read tree from nonexistent directory.");
            tree = dynamic_cast<TTree*>(directory->Get(name.c_str()));
            if(!tree)
                throw std::runtime_error("Tree not found.");
            if(tree->GetNbranches())
                tree->SetBranchStatus("*", 0);
        } else {
            tree = new TTree(name.c_str(), name.c_str());
            tree->SetDirectory(directory);
            if(directory)
                tree->SetMaxVirtualSize(maxVirtualSize);
        }
    }

    SmartTree(const SmartTree&& other)
        : name(other.name), directory(other.directory), entries(other.entries), tree(other.tree),
          disabled_branches(other.disabled_branches), enabled_branches(other.enabled_branches) {}

    virtual ~SmartTree()
    {
        if(directory) directory->Delete(name.c_str());
        else delete tree;
    }

    void Fill()
    {
        std::lock_guard<std::mutex> lock(mutex);
        tree->Fill();
        for(auto& entry : entries)
            entry.second->clear();
    }

    Long64_t GetEntries() const { return tree->GetEntries(); }
    Long64_t GetReadEntry() const { return tree->GetReadEntry(); }

    Int_t GetEntry(Long64_t entry)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return tree->GetEntry(entry);
    }

    void Write()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(directory)
            directory->WriteTObject(tree, tree->GetName(), "Overwrite");
    }

    std::mutex& GetMutex() { return mutex; }

protected:
    template<typename DataType>
    void AddBranch(const std::string& branch_name, DataType& value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!disabled_branches.count(branch_name) && (!enabled_branches.size() || enabled_branches.count(branch_name))){
            detail::BranchCreator<DataType> creator;
            creator.Create(*tree, branch_name, value, readMode, entries);
        }
    }

    bool HasBranch(const std::string& branch_name) const
    {
        return entries.count(branch_name) != 0;
    }


private:
    SmartTree(const SmartTree& other) { throw std::runtime_error("Can't copy a smart tree"); }

private:
    std::string name;
    TDirectory* directory;
    detail::SmartTreeEntryMap entries;
    bool readMode;
    TTree* tree;
    std::set<std::string> disabled_branches;
    std::set<std::string> enabled_branches;
    std::mutex mutex;
};

namespace detail {
template<typename Data>
class BaseSmartTree : public SmartTree {
public:
    using SmartTree::SmartTree;
    BaseSmartTree(const BaseSmartTree&& other)
        : SmartTree(other), _data(other._data) {}

    Data& operator()() { return *_data; }
    const Data& operator()() const { return *_data; }
    const Data& data() const { return *_data; }

protected:
    std::shared_ptr<Data> _data{new Data()};
};
} // detail
} // root_ext

