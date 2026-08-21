#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/CommandLine.h"

#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

// =============================================================================

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult& result)
{
  auto& diag = result.Context->getDiagnostics();

  // Получаем SourceManager для проверки isInMainFile
  auto& sm = *result.SourceManager;

  if (const auto* dtor = result.Nodes.getNodeAs<CXXDestructorDecl>("nvDtorDecl"))
  {
    SourceLocation loc = dtor->getLocation();

    if (loc.isValid() && sm.isInSystemHeader(loc))
    {
      // This declaration is from a system header, so we skip processing it.
      return;
    }

    handle_nv_dtor(dtor, diag, sm);
  }

  if (const auto* method = result.Nodes.getNodeAs<CXXMethodDecl>("methodDecl");
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

  if (const auto* loopVar = result.Nodes.getNodeAs<VarDecl>("VarDecl"))
  {
    SourceLocation loc = loopVar->getLocation();

    if (loc.isValid() && sm.isInSystemHeader(loc))
    {
      // This declaration is from a system header, so we skip processing it.
      return;
    }

    handle_crange_for(loopVar, diag, sm);
  }
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

// TODO: необходимо реализовать обработку случая отсутствие override
void RefactorHandler::handle_miss_override(const CXXMethodDecl* Method,
                                           DiagnosticsEngine& Diag,
                                           SourceManager& SM)
{
  /*
  const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark,
                                               "Объявлен метод");
  Diag.Report(Method->getLocation(), DiagID);
  */
}

// =============================================================================

//todo: необходимо реализовать обработку случая отсутствие & в range-for
void RefactorHandler::handle_crange_for(const VarDecl* LoopVar,
                                        DiagnosticsEngine& Diag,
                                        SourceManager& SM)
{
  /*
  const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark,
                                               "Объявлена переменная");
  Diag.Report(LoopVar->getLocation(), DiagID);
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
  ).bind("nvDtorDecl");
}

// =============================================================================

auto NoOverrideMatcher()
{
  // todo: замените код ниже, на свою реализацию, необходимо реализовать матчеры
  // для поиска методов без override
  return cxxMethodDecl().bind("methodDecl");
}

// =============================================================================

auto NoRefConstVarInRangeLoopMatcher()
{
  // todo: замените код ниже, на свою реализацию, необходимо реализовать матчеры
  // для поиска range-for без &
  return varDecl().bind("VarDecl");
}

// =============================================================================

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter& Rewrite) : Handler(Rewrite)
{
  // Создаем MatchFinder и добавляем матчеры.
  Finder.addMatcher(NvDtorMatcher(), &Handler);
  Finder.addMatcher(NoOverrideMatcher(), &Handler);
  Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

// =============================================================================

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext& Context)
{
  Finder.matchAST(Context);
}

// =============================================================================

std::unique_ptr<ASTConsumer>
CodeRefactorAction::CreateASTConsumer(CompilerInstance& CI,
                                      StringRef file)
{
  _file = file;
  RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
  return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

// =============================================================================

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance& CI)
{
  // Инициализируем Rewriter для рефакторинга.
  RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(),
                                       CI.getLangOpts());
  // Возвращаем true, чтобы продолжить обработку файла.
  return true;
}

// =============================================================================

void CodeRefactorAction::EndSourceFileAction()
{
  // Применяем изменения в файле.
  if (RewriterForCodeRefactor.overwriteChangedFiles())
  {
    llvm::errs() << "Error applying changes to files.\n";
  }
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
  return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}
