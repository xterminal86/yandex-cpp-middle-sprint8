#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/CommandLine.h"

#include <unordered_set>
#include <iostream>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

const char* kNvDtorMatcherName          = "nvDtorDecl";
const char* kMissingOverrideMatcherName = "missingOverride";
const char* kLoopVarMatcherName         = "loopVar";
const char* kForRangeMatcherName        = "forRange";

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

// =============================================================================

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult& result)
{
  /*
  auto& diag = result.Context->getDiagnostics();

  // Получаем SourceManager для проверки isInMainFile
  auto& sm = *result.SourceManager;

  if (const auto* dtor =
    result.Nodes.getNodeAs<CXXDestructorDecl>(kNvDtorMatcherName))
  {
    SourceLocation loc = dtor->getLocation();

    if (loc.isValid() && sm.isInSystemHeader(loc))
    {
      // This declaration is from a system header, so we skip processing it.
      return;
    }

    handle_nv_dtor(dtor, diag, sm);
  }

  if (const auto* method =
      result.Nodes.getNodeAs<CXXMethodDecl>(kMissingOverrideMatcherName);
      method && method->size_overridden_methods() > 0
  && !method->hasAttr<OverrideAttr>())
  {
    SourceLocation loc = method->getLocation();

    if (loc.isValid() && sm.isInSystemHeader(loc))
    {
      // This declaration is from a system header, so we skip processing it.
      return;
    }

    handle_miss_override(method, diag, sm);
  }

  if (const auto* loopVar =
    result.Nodes.getNodeAs<VarDecl>(kLoopVarMatcherName))
  {
    SourceLocation loc = loopVar->getLocation();

    if (loc.isValid() && sm.isInSystemHeader(loc))
    {
      // This declaration is from a system header, so we skip processing it.
      return;
    }

    handle_crange_for(loopVar, diag, sm);
  }
  */
}

// =============================================================================

void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl* dtorDecl,
                                     DiagnosticsEngine& diag,
                                     SourceManager& sm)
{
  const CXXRecordDecl* record = dtorDecl->getParent();
  if (!record || !record->hasDefinition())
  {
    return;
  }

  // Skip if this is an implicit destructor (compiler-generated)
  if (dtorDecl->isImplicit())
  {
    return;
  }

  bool hasDerivedClasses = false;

  // Traverse the AST to find classes that derive from 'record'
  for (auto* derivedRecord :
    record->getASTContext().getTranslationUnitDecl()->decls())
  {
    if (auto* derivedClass = dyn_cast<CXXRecordDecl>(derivedRecord))
    {
      // Skip if it's the same class or doesn't have a definition
      if (derivedClass == record || !derivedClass->hasDefinition())
      {
        continue;
      }

      // Get the definition if it's a forward declaration
      const auto* def = derivedClass->getDefinition();
      if (!def)
      {
        continue;
      }

      for (const auto& base : def->bases())
      {
        if (const auto* baseRecord = base.getType()->getAsCXXRecordDecl())
        {
          if (baseRecord->getCanonicalDecl() == record->getCanonicalDecl())
          {
            hasDerivedClasses = true;
            // Страуструп разрешил.
            goto _exit_for;
          }
        }
      }
    }
  }

_exit_for:

  if (!hasDerivedClasses)
  {
    return; // No derived classes, skip
  }

  if (dtorDecl->isVirtual())
  {
    return; // Already virtual
  }

  SourceLocation insertLoc = dtorDecl->getBeginLoc();
  if (insertLoc.isValid())
  {
    Rewrite.InsertText(insertLoc, "virtual ", true, true);
  }

  /*
  const unsigned diagID = diag.getCustomDiagID(DiagnosticsEngine::Remark,
                                               "Объявлен деструктор");
  diag.Report(dtorDecl->getLocation(), diagID);
  */
}

// =============================================================================

void RefactorHandler::handle_miss_override(const CXXMethodDecl* method,
                                           DiagnosticsEngine& diag,
                                           SourceManager& sm)
{
  if (!method)
  {
    return;
  }

  // 1. Skip destructors
  if (isa<CXXDestructorDecl>(method))
  {
    return;
  }

  // 2. Skip constructors
  if (isa<CXXConstructorDecl>(method))
  {
    return;
  }

  // 3. Skip if it's a copy/move operator
  if (method->isCopyAssignmentOperator() ||
      method->isMoveAssignmentOperator())
  {
    return;
  }

  // 4. Skip if it's an implicit method
  if (method->isImplicit())
  {
    return;
  }

  // 5. Check if it overrides anything
  bool hasOverridden = false;
  for (auto it = method->begin_overridden_methods();
      it != method->end_overridden_methods(); ++it)
  {
    hasOverridden = true;
    break;
  }

  if (!hasOverridden)
  {
    return;
  }

  // 6. Skip if it's not virtual
  if (!method->isVirtual())
  {
    return;
  }

  // 7. Skip if the method is in a class with no base class
  const CXXRecordDecl* record = method->getParent();
  if (!record
  || (!record->hasAnyDependentBases() && record->getNumBases() == 0))
  {
    return;
  }

  // 8. Skip if already has override
  if (method->hasAttr<clang::OverrideAttr>())
  {
    return;
  }

  // 9. Check if the method has no body and is pure
  if (method->isPureVirtual() && !method->getBody())
  {
    return;
  }

  // Insert "override" before the function body
  SourceLocation insertLoc;

  // Try to get the location before the body
  if (method->getBody())
  {
    // If the method has a body, insert before it
    insertLoc = method->getBody()->getBeginLoc();
  }
  else
  {
    // For declarations without body (e.g., in header files)
    // Insert at the end of the declaration
    insertLoc = method->getEndLoc();
  }

  if (insertLoc.isValid())
  {
    // Check if there's already a space before the insertion point
    // We need to handle this carefully to avoid double spaces
    Rewrite.InsertText(insertLoc, "override ", true, true);
  }

  /*
  const unsigned diagID = diag.getCustomDiagID(DiagnosticsEngine::Remark,
                                               "Объявлен метод");
  diag.Report(method->getLocation(), diagID);
  */
}

// =============================================================================

void RefactorHandler::handle_crange_for(const VarDecl* loopVar,
                                        DiagnosticsEngine& diag,
                                        SourceManager& sm)
{
  if (!loopVar)
  {
    return;
  }

  // Skip if this is a reference type (shouldn't happen due to matcher, but
  // check anyway)
  if (loopVar->getType()->isReferenceType())
  {
    return;
  }

  // Skip const references (they're fine)
  if (!loopVar->getType().isConstQualified())
  {
    return;
  }

  // Skip fundamental types (int, float, char, etc.)
  QualType type = loopVar->getType();
  if (type->isFundamentalType())
  {
    return;
  }

  // Get the actual declaration source range
  SourceRange range = loopVar->getSourceRange();
  SourceLocation startLoc = range.getBegin();

  if (!startLoc.isValid())
  {
    return;
  }

  // Find where to insert the '&'
  // We want to insert after the type name, before the variable name
  // For "const auto x", we want "const auto& x"
  // For "const CustomType x", we want "const CustomType& x"
  SourceLocation insertLoc = loopVar->getLocation();

  // Insert '&' before the variable name
  // This will produce "const auto& x" or "const CustomType& x"
  Rewrite.InsertText(insertLoc, "&", true, true);

  /*
  const unsigned diagID = diag.getCustomDiagID(DiagnosticsEngine::Remark,
                                               "Объявлена переменная");
  diag.Report(loopVar->getLocation(), diagID);
  */
}

// =============================================================================

//todo: ниже необходимо реализовать матчеры для поиска узлов AST
//note: синтаксис написания матчеров точно такой же как и для использования
//clang-query
/*
    Пример того, как может выглядеть реализация:
    auto AllClassesMatcher()
    {
        return cxxRecordDecl().bind("classDecl");
    }
*/

// =============================================================================

auto NvDtorMatcher()
{
  return cxxDestructorDecl(
    unless(isVirtual()),
    ofClass(cxxRecordDecl().bind("class"))
  ).bind(kNvDtorMatcherName);
}

// =============================================================================

auto NoOverrideMatcher()
{
  return cxxMethodDecl(
    isOverride(),
    unless(hasAttr(clang::attr::Override)),
    // Exclude destructors by checking if the name doesn't start with "~"
    unless(hasName("~"))
  ).bind(kMissingOverrideMatcherName);
}

// =============================================================================

auto NoRefConstVarInRangeLoopMatcher()
{
  return varDecl(
    hasAncestor(cxxForRangeStmt().bind(kForRangeMatcherName)),
    unless(hasType(referenceType()))
  ).bind(kLoopVarMatcherName);
}

// =============================================================================

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter& Rewrite) : Handler(Rewrite)
{
  // Создаем MatchFinder и добавляем матчеры.
  //Finder.addMatcher(NvDtorMatcher(), &Handler);
  //Finder.addMatcher(NoOverrideMatcher(), &Handler);
  //Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

// =============================================================================

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext& Context)
{
  //Finder.matchAST(Context);
}

// =============================================================================

std::unique_ptr<ASTConsumer>
CodeRefactorAction::CreateASTConsumer(CompilerInstance& CI,
                                      StringRef file)
{
  //RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
  //return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
  //return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);

  // FIXME: debug
  return std::make_unique<clang::ASTConsumer>();
}

// =============================================================================

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance& CI)
{
  /*
  // Инициализируем Rewriter для рефакторинга.
  RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(),
                                       CI.getLangOpts());
  // Возвращаем true, чтобы продолжить обработку файла.
  return true;
  */

  return true;
}

// =============================================================================

void CodeRefactorAction::EndSourceFileAction()
{
  // Применяем изменения в файле.
  //if (RewriterForCodeRefactor.overwriteChangedFiles())
  //{
  //  llvm::errs() << "Error applying changes to files.\n";
  //}
}

// =============================================================================

int main(int argc, const char **argv)
{
  // Парсер опций: Обрабатывает флаги командной строки, компиляционные базы
  // данных.
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
  if (!ExpectedParser)
  {
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser& OptionsParser = ExpectedParser.get();
  // Создаем ClangTool
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());
  // Запускаем RefactorAction.
  std::unique_ptr<FrontendActionFactory> fucktory =
    newFrontendActionFactory<CodeRefactorAction>();
  return Tool.run(fucktory.get());
}
